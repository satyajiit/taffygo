// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/taffy_render_frame_observer.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"

// VERIFY AT SP-04 - the RenderFrameObserver surface moves between milestones
// and every item here is a compile error rather than a silent behaviour
// change, which is the good kind of uncertainty:
//   * content::RenderFrameObserver::DidCommitProvisionalLoad() signature.
//   * Whether DidCreateNewDocument() fires for the initial empty document and
//     for same-document navigations. Getting this wrong means either an
//     endpoint per about:blank or a missed epoch boundary, and the second is
//     a security bug. The BFCache and prerender fixtures in the corpus are
//     what settle it.
//   * blink::AssociatedInterfaceRegistry::AddInterface<T>() spelling and
//     whether RenderFrame::GetAssociatedInterfaceRegistry() is still the
//     accessor.
//   * content::RenderFrameObserver::FocusedElementChanged() and
//     DidObserveLayoutShift() signatures.
//   * Which observer callback signals back/forward cache entry and restore.
//     Spec section 13 needs both, and whether a restore always allocates a
//     new epoch is [Open (OD-029)] - this file assumes it does, which is the
//     safe assumption, and the spike may relax it with evidence.

namespace taffy {

namespace {

// Renderer-local, per-process, monotonic. Never leaves this process; see the
// comment on local_frame_id_.
FrameId NextLocalFrameId() {
  static uint64_t next = 1;
  return FrameId(base::StrCat({"rf", base::NumberToString(next++)}));
}

}  // namespace

TaffyRenderFrameObserver::TaffyRenderFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame),
      local_frame_id_(NextLocalFrameId()) {
  render_frame->GetAssociatedInterfaceRegistry()
      ->AddInterface<mojom::PageIntelligence>(base::BindRepeating(
          &TaffyRenderFrameObserver::BindEndpoint, base::Unretained(this)));
}

TaffyRenderFrameObserver::~TaffyRenderFrameObserver() = default;

void TaffyRenderFrameObserver::OnDestruct() {
  RetireEndpoint(mojom::InvalidationReason::kFrameDetached);
  delete this;
}

void TaffyRenderFrameObserver::BindEndpoint(
    mojo::PendingAssociatedReceiver<mojom::PageIntelligence> receiver) {
  if (!endpoint_) {
    endpoint_ = std::make_unique<PageIntelligenceEndpoint>(
        render_frame()->GetWebFrame(), local_frame_id_);
    // The endpoint's lifecycle starts as an empty optional and every guard in
    // it compares against kActive, which an empty optional fails - so an
    // endpoint that has never been told a lifecycle refuses every observation,
    // subscription and action with kDocumentInactive. That refusal is correct
    // and deliberate (renderer/README.md item 4: there is no default, because
    // defaulting would be a renderer inventing a browser-owned fact); this
    // seed is the initial statement of the fact.
    //
    // The statement is kActive, and deliberately NOT a visibility reading.
    // Visibility is not lifecycle: content's own
    // RenderFrameHost::LifecycleState stays kActive while a frame is hidden,
    // and the one consumer this gate exists for reads pages that are covered
    // on purpose - the task surface (SCR-302/303) sits over the page while
    // Taffy reads it, so a hidden-means-frozen mapping refused every read the
    // product can actually issue (measured on the phone: observation result
    // kDocumentInactive with the page loaded and covered). A document that is
    // genuinely not active - back-forward cached, pending deletion,
    // prerendering - is refused by the browser's own gate from the
    // RenderFrameHost lifecycle before this endpoint is ever asked, which is
    // the browser-owned fact; a frame being repainted or not is a compositor
    // detail this protocol does not gate on.
    endpoint_->OnLifecycleChanged(mojom::DocumentLifecycleState::kActive);
  }
  endpoint_->Bind(std::move(receiver));
}

void TaffyRenderFrameObserver::RetireEndpoint(
    mojom::InvalidationReason reason) {
  if (!endpoint_) {
    return;
  }
  endpoint_->Invalidate(reason);
  endpoint_.reset();
}

void TaffyRenderFrameObserver::DidCreateNewDocument() {
  // A new document is a new epoch. The old endpoint is destroyed here, before
  // anything can observe the new document, so there is no window in which a
  // handle from the previous document could be resolved against this one.
  RetireEndpoint(mojom::InvalidationReason::kCrossDocumentCommit);
}

void TaffyRenderFrameObserver::DidCommitProvisionalLoad(
    ui::PageTransition transition) {
  RetireEndpoint(mojom::InvalidationReason::kCrossDocumentCommit);
}

void TaffyRenderFrameObserver::DidDispatchDOMContentLoadedEvent() {
  // The parser is done, so the document now has whatever body it is going to
  // have. A whole-document read that arrived before this point is waiting on
  // it — see PageIntelligenceEndpoint::DocumentIsStillArriving.
  if (endpoint_) {
    endpoint_->OnDocumentParsed();
  }
}

void TaffyRenderFrameObserver::DidChangeScrollOffset() {
  // Scrolling changes what is visible, and visibility is an action
  // precondition (protocol section 11.4). It is a layout-class signal, so it is
  // the first thing dropped under backpressure - but it is not free to
  // ignore, because an action checked against "visible" before a scroll must
  // not dispatch after one.
  if (endpoint_) {
    // No node id: scrolling changes what is visible for the whole document,
    // and naming one node would be a claim this callback cannot support.
    endpoint_->OnDocumentMutated(
        SemanticGraphStore::ChangeClass::kVisibilityChanged, SemanticNodeId());
  }
}

void TaffyRenderFrameObserver::FocusedElementChanged(
    const blink::WebElement& element) {
  // Focus and selection are accessible state (protocol section 5.3), and both
  // are action preconditions: a caller that observed a control as focused and
  // dispatches after focus moved is acting on a different assumption than the
  // one it checked. The element itself is deliberately not inspected here -
  // this callback only says that something changed, and the adapters are what
  // describe it.
  if (endpoint_) {
    endpoint_->OnDocumentMutated(
        SemanticGraphStore::ChangeClass::kAccessibleStateChanged,
        SemanticNodeId());
  }
}

void TaffyRenderFrameObserver::DidObserveLayoutShift(
    double score,
    bool after_input_or_scroll) {
  // Bounds moved. This is the lowest-priority delta class and the first thing
  // dropped under backpressure, but it is not free to ignore: the precondition
  // check compares a target's recorded bounds, and a consent dialog that slid
  // a different button under the point a plan was made about is exactly the
  // shape that comparison exists to catch.
  //
  // VERIFY AT SP-04: content::RenderFrameObserver::DidObserveLayoutShift()
  // exists with this signature at the pin, and whether it fires often enough
  // on a busy page to matter for the delta budget. If it is too chatty, the
  // signal has to come from the layout adapter's own comparison instead,
  // which is strictly less timely and strictly cheaper.
  if (endpoint_) {
    endpoint_->OnDocumentMutated(
        SemanticGraphStore::ChangeClass::kBoundsChanged, SemanticNodeId());
  }
}

// WasHidden()/WasShown() are deliberately not overridden. They report
// visibility, and visibility is not lifecycle - see the seed comment in
// BindEndpoint(): the task surface covers the page while Taffy reads it, so a
// hidden frame must stay observable, exactly as content's own
// RenderFrameHost::LifecycleState says it is.

}  // namespace taffy
