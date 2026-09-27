// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/page_intelligence_endpoint.h"

#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/page_capabilities.h"
#include "taffy/renderer/snapshot_builder.h"
#include "taffy/renderer/wire_conversions.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"

// VERIFY AT SP-04:
//   * That mojo::AssociatedReceiver is the flavor the broker binds - it uses
//     RenderFrameHost::GetRemoteAssociatedInterfaces(). `[Open (OD-027)]`.
//   * blink::WebLocalFrame::Parent() is read in snapshot_builder.cc rather
//     than here; the origin and URL diagnostics moved there with the
//     envelope, and their VERIFY notes moved with them.

namespace taffy {

namespace {

// The protocol version this endpoint speaks.
//
// taffy-core/contracts/bip/schema/bip.version.json owns the value;
// //taffy/components/intelligence/content/bip_schema_version.h is the browser
// process's copy of it. This is the second C++ copy, and it exists only because
// the renderer must not include a browser header (see DEPS) - the alternative
// would be a shared header, emitted somewhere the renderer may include, which
// the contracts generator does not do yet.
//
// The skew this comment used to warn about happened. This line said "0.11"
// while the contract reached 0.13, and ProtocolNegotiator::OnProtocolInfo
// refuses any reply whose version is not the browser's own — so every protocol
// negotiation in every binary settled as UNSUPPORTED, in the product as much as
// in the tests, and RendererEndpointRequirement failed on every correctness and
// benchmark browser test for two minor steps. Both intervening steps were
// additive and this endpoint already implements the only one that touches it:
// 0.12's challenge_kind, which the semantic graph emits and classifies. 0.11
// and 0.13 add a browser-side result code and a browser-owned postcondition,
// and 0.14 adds DOCUMENT_ADVANCED, which the browser settles from its own
// navigation events and from a snapshot taken after dispatch. None of the three
// has a renderer operand.
//
// The warning is now a gate rather than a comment: layout.py's
// VERSION_RESTATEMENTS names this file and generate.py --check fails when this
// literal and bip.version.json disagree. Keep the declaration matching the
// pattern there, or the check reports it as unreadable rather than passing.
constexpr char kProtocolVersion[] = "0.14";
constexpr char kImplementationId[] = "taffy-renderer";

// How long a snapshot waits for a document that is still being parsed.
//
// The request's deadline is the browser's deadline too — it arms an identical
// one and answers kDeadlineExceeded when it runs out — so waiting for all of
// it would settle the two at the same instant and make which answer the caller
// gets a race. Three quarters leaves the read itself, which on a document with
// no body is nearly free, inside the bound the caller asked for.
base::TimeDelta WaitForDocument(const mojom::SnapshotRequest& request) {
  return base::Milliseconds(request.deadline_ms) * 3 / 4;
}

}  // namespace

PageIntelligenceEndpoint::PageIntelligenceEndpoint(blink::WebLocalFrame* frame,
                                                   FrameId frame_id)
    : frame_(frame), frame_id_(std::move(frame_id)) {
  CHECK(frame_);
}

PageIntelligenceEndpoint::~PageIntelligenceEndpoint() {
  // Every path here comes through Invalidate, which answers a held request
  // already. This is the one that would not: dropping a mojo reply callback
  // closes the receiver as a protocol error, and an endpoint tearing down is
  // not a reason to fault the pipe. Nothing on the frame is read — it may
  // already be gone — so the answer is the bare one.
  if (deferred_snapshot_callback_) {
    deferred_snapshot_timer_.Stop();
    auto result = mojom::SnapshotResult::New();
    if (deferred_snapshot_) {
      result->request_id = deferred_snapshot_->request_id;
    }
    result->code = mojom::ObservationResultCode::kDocumentInactive;
    std::move(deferred_snapshot_callback_).Run(std::move(result));
  }
}

SemanticGraphStore* PageIntelligenceEndpoint::StoreFor(PageEpoch epoch) {
  if (!store_) {
    store_ = std::make_unique<SemanticGraphStore>(frame_id_, epoch);
    // The document's own changes. Nothing in //content or Blink's public
    // interfaces reports a DOM mutation to a RenderFrameObserver, so without
    // this the graph revision would advance only on scroll, focus and layout
    // shift - and a subscriber could watch a page rewrite itself and never be
    // told (chromium/patches/0026-report-dom-mutations-to-the-embedder.md).
    dom_mutations_ = std::make_unique<DomMutationSignalSource>(
        frame_, store_.get(),
        base::BindRepeating(&PageIntelligenceEndpoint::OnDocumentMutated,
                            weak_factory_.GetWeakPtr()),
        base::BindRepeating(&PageIntelligenceEndpoint::OnNodesRemoved,
                            weak_factory_.GetWeakPtr()));
    return store_.get();
  }
  return store_->page_epoch() == epoch ? store_.get() : nullptr;
}

std::optional<DocumentLifecycle> PageIntelligenceEndpoint::LifecycleFact()
    const {
  if (!lifecycle_state_.has_value()) {
    return std::nullopt;
  }
  return wire::FromMojom(lifecycle_state_.value());
}

bool PageIntelligenceEndpoint::DocumentIsStillArriving() const {
  if (!frame_) {
    return false;
  }
  blink::WebDocument document = frame_->GetDocument();
  if (document.IsNull()) {
    // No document at all yet. There is nothing here to describe and nothing
    // to say about why, which is the clearest case of "not arrived".
    return true;
  }
  if (!document.Body().IsNull()) {
    return false;
  }
  // A body-less document whose parser has finished is a document that has no
  // body — an image or a plugin document, say. That is a true unsupported and
  // waiting for it would only spend the caller's deadline.
  return !document.IsLoaded();
}

void PageIntelligenceEndpoint::OnDocumentParsed() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RunDeferredSnapshot();
}

void PageIntelligenceEndpoint::RunDeferredSnapshot() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!deferred_snapshot_callback_) {
    return;
  }
  deferred_snapshot_timer_.Stop();
  // `may_wait` is false: this request has had its wait. Every other check runs
  // again from the top, because the document it was held for may have gone
  // inactive or changed epoch while it waited, and this is not permission to
  // answer for a different one.
  BeginSnapshot(std::move(deferred_snapshot_),
                std::move(deferred_snapshot_callback_), /*may_wait=*/false);
}

BrowserSuppliedFacts PageIntelligenceEndpoint::FactsFrom(
    const mojom::SnapshotRequest& request) const {
  BrowserSuppliedFacts facts;
  facts.tab_id = request.tab_id;
  facts.root_frame_id = request.root_frame_id;
  facts.page_epoch = request.expected_page_epoch.value_or(std::string());
  facts.sensitivity_policy_id = request.sensitivity_policy_id;
  facts.task_purpose = request.task_purpose;
  facts.lifecycle = LifecycleFact();
  for (const mojom::OriginPtr& origin : request.allowed_origins) {
    if (origin && origin->kind == mojom::OriginKind::kTuple &&
        origin->serialization.has_value()) {
      facts.allowed_origin_serializations.push_back(
          origin->serialization.value());
    }
  }
  // A frame the broker did not name as the root of this observation is a
  // child frame, and a child frame is treated as cross-origin unless the
  // broker said otherwise. The renderer must not decide this for itself:
  // its own view of the origin is exactly what protocol section 7.2 says
  // does not override the browser's record.
  //
  // Both sides of this comparison are therefore the broker's. They used to
  // not be: the right-hand side was `frame_id_`, this endpoint's own identity,
  // which is a value the renderer minted for itself in a namespace the browser
  // does not share (taffy_render_frame_observer.h). The two could never be
  // equal, so every document was classified as a cross-origin frame — the main
  // frame of a first-party page included — every node's sensitivity floor was
  // raised to kUnknownSensitive, and the graph payload then withheld every
  // name on every page. It failed in the safe direction, which is why it cost
  // a feature rather than a secret, and it was still a check that was
  // deciding nothing.
  //
  // `observed_frame_id` is optional under the additive rule, so an older
  // broker may not send it. That is not licence to assume the convenient
  // answer: not being told which frame this is means the floor goes up, the
  // same as for a genuine child.
  facts.cross_origin_frame =
      !request.observed_frame_id.has_value() ||
      request.root_frame_id != request.observed_frame_id.value();
  // A cross-origin frame, an insecure context, or a document the browser has
  // not called active all raise the floor every classification starts from.
  // Only the browser's own signals do this; nothing page-authored can.
  if (facts.cross_origin_frame ||
      lifecycle_state_ != mojom::DocumentLifecycleState::kActive) {
    facts.policy_floor = Sensitivity::kUnknownSensitive;
  }
  return facts;
}

BrowserSuppliedFacts PageIntelligenceEndpoint::CurrentFacts() const {
  BrowserSuppliedFacts facts;
  facts.tab_id = tab_id_;
  // The browser's identity for this document, remembered from the last thing
  // the browser said, rather than the endpoint's own. A renderer-minted value
  // here would be a renderer naming a frame to a consumer that resolves frames
  // in the browser's namespace, which is a name that resolves to nothing.
  facts.root_frame_id = browser_frame_id_;
  facts.page_epoch = store_ ? store_->page_epoch().value() : std::string();
  facts.sensitivity_policy_id = sensitivity_policy_id_;
  facts.task_purpose = task_purpose_;
  facts.lifecycle = LifecycleFact();
  facts.allowed_origin_serializations = allowed_origin_serializations_;
  // Carried rather than recomputed. A delta describes changes to the document
  // a snapshot already established the facts for, and a delta path that
  // defaulted this to false would redact less than the snapshot it is
  // updating — the same page, the same nodes, two different floors.
  facts.cross_origin_frame = cross_origin_frame_;
  if (facts.cross_origin_frame ||
      lifecycle_state_ != mojom::DocumentLifecycleState::kActive) {
    facts.policy_floor = Sensitivity::kUnknownSensitive;
  }
  return facts;
}

void PageIntelligenceEndpoint::Bind(
    mojo::PendingAssociatedReceiver<mojom::PageIntelligence> receiver) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  receiver_.reset();
  receiver_.Bind(std::move(receiver));
}

void PageIntelligenceEndpoint::GetProtocolInfo(
    GetProtocolInfoCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto info = mojom::ProtocolInfo::New();
  info->protocol_version = kProtocolVersion;
  info->implementation_id = kImplementationId;

  // CAP-PI-008. Computed for THIS document rather than reported from the
  // build, because whether the selection adapter can say anything depends on
  // whether the user has selected something, and whether the layout adapter
  // can depends on whether this frame has widget geometry. Reporting the
  // build-time list would be reporting what the binary contains.
  const PageCapabilities capabilities = PageCapabilities::ForDocument(
      CollectCapabilitySignals(frame_, CurrentFacts()));

  // Every adapter this endpoint runs can be named, so what is advertised is
  // exactly what this document supports. Selection and layout were once
  // unadvertisable at all - mojom::AdapterKind had no member for either, and
  // ProtocolInfo has no warning channel to describe them in - which meant a
  // caller could not ask for the two adapters this endpoint had. The additive
  // minor contract step recorded in
  // taffy-core/contracts/bip/schema/bip.version.json appended SELECTION and
  // LAYOUT and closed that.
  for (const AdapterCapability& capability : capabilities.adapters()) {
    if (capability.availability == AdapterAvailability::kUnsupported) {
      continue;
    }
    info->supported_adapters.push_back(wire::ToMojom(capability.kind));
  }

  for (ExtractionScope scope :
       {ExtractionScope::kViewport, ExtractionScope::kInteractive,
        ExtractionScope::kSelection, ExtractionScope::kSection,
        ExtractionScope::kDocument}) {
    if (capabilities.SupportsScope(scope)) {
      info->supported_scopes.push_back(wire::ToMojom(scope));
    }
  }

  // Implementation support only. Per-node actions remain narrower: the form
  // adapter advertises a write only on an enabled, visible-by-schema,
  // non-prohibited control of the exact corresponding type. ProtocolInfo is
  // never authorization; the browser still consumes a matching capability,
  // journals, and re-resolves that node before dispatch.
  info->supported_action_types = {
      mojom::ActionType::kScrollIntoView, mojom::ActionType::kFocus,
      mojom::ActionType::kActivate,       mojom::ActionType::kSetText,
      mojom::ActionType::kSelectOption,   mojom::ActionType::kToggle,
      mojom::ActionType::kSubmitForm,
  };
  info->supported_redaction_features = {
      mojom::RedactionFeature::kSecretValueSuppression,
      mojom::RedactionFeature::kSensitiveZoneClassification,
      mojom::RedactionFeature::kPatternDetectors,
      mojom::RedactionFeature::kContentTrustLabelling,
      mojom::RedactionFeature::kInjectionSignalDetection,
  };

  const ObservationLimits& limits = ObservationLimits::ProcessSafeCeiling();
  info->limits = mojom::ProtocolLimits::New();
  info->limits->max_message_bytes = limits.snapshot().max_total_bytes();
  info->limits->max_nodes = limits.snapshot().max_nodes();
  info->limits->max_text_bytes = limits.snapshot().max_text_bytes();
  info->limits->max_total_bytes = limits.snapshot().max_total_bytes();
  info->limits->max_depth = limits.snapshot().max_depth();
  info->limits->max_frames = limits.snapshot().max_frames();
  info->limits->max_delta_queue_depth = limits.delta().max_queued_signals();
  info->limits->max_snapshot_deadline_ms = limits.snapshot().deadline_ms();
  info->limits->min_delta_interval_ms = limits.delta().min_interval_ms();

  std::move(callback).Run(std::move(info));
}

void PageIntelligenceEndpoint::GetSnapshot(mojom::SnapshotRequestPtr request,
                                           GetSnapshotCallback callback) {
  BeginSnapshot(std::move(request), std::move(callback), /*may_wait=*/true);
}

void PageIntelligenceEndpoint::BeginSnapshot(mojom::SnapshotRequestPtr request,
                                             GetSnapshotCallback callback,
                                             bool may_wait) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto result = mojom::SnapshotResult::New();
  result->request_id = request->request_id;

  if (invalidated_ ||
      lifecycle_state_ != mojom::DocumentLifecycleState::kActive) {
    result->code = mojom::ObservationResultCode::kDocumentInactive;
    std::move(callback).Run(std::move(result));
    return;
  }

  // A read that arrived before the document did waits for it.
  //
  // Not a courtesy: the answer this would otherwise give is wrong about what
  // happened. A whole-document read REQUIRES the DOM adapter, that adapter
  // reports unsupported while `document.Body()` is null, and one unsupported
  // required adapter makes the whole observation kUnsupported — the code whose
  // recovery is do-not-retry. A phone measured the cost on 2026-09-19: an
  // errand followed a link to the site it wanted, the browser verified the
  // navigation at commit and read the page 25 ms later, and this endpoint
  // answered that the build could not describe it. The walk asked twice more
  // within 210 ms, and the errand ended "could not read enough to answer" on a
  // page that finished parsing a second afterwards. Decision 0196.
  //
  // The wait ends at the parser, and the timer is only the ceiling on a
  // document that never gets there.
  if (may_wait && !deferred_snapshot_callback_ && DocumentIsStillArriving()) {
    deferred_snapshot_timer_.Start(
        FROM_HERE, WaitForDocument(*request),
        base::BindOnce(&PageIntelligenceEndpoint::RunDeferredSnapshot,
                       weak_factory_.GetWeakPtr()));
    deferred_snapshot_ = std::move(request);
    deferred_snapshot_callback_ = std::move(callback);
    return;
  }

  // The caller states which document it believes it is talking to, and it has
  // to: the epoch is the node id namespace, and this endpoint will not
  // allocate ids into a namespace it cannot name. A mismatch is refused
  // rather than answered for the current document, because answering would
  // silently rebind a task to a page it never observed.
  //
  // The browser states the epoch on every request, first bind included
  // (README.md item 1, settled 2026-08-20). This refusal is therefore the
  // defence-in-depth path rather than the ordinary one, and it stays: a
  // browser half that stopped stating the epoch would otherwise get ids
  // allocated into a namespace nobody named. It stays exercised by
  // AdapterTest.MissingEpochIsRefusedNotInvented in
  // renderer/test/adapter_render_view_test.cc, which drives this endpoint
  // directly with std::nullopt.
  if (!request->expected_page_epoch.has_value()) {
    result->code = mojom::ObservationResultCode::kStalePageEpoch;
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kAdapterUnavailable;
    warning->detail_code = "page-epoch-required-on-bind";
    result->warnings.push_back(std::move(warning));
    std::move(callback).Run(std::move(result));
    return;
  }
  if (!StoreFor(PageEpoch(request->expected_page_epoch.value()))) {
    result->code = mojom::ObservationResultCode::kStalePageEpoch;
    std::move(callback).Run(std::move(result));
    return;
  }

  negotiated_schema_version_ = request->schema_version;
  tab_id_ = request->tab_id;
  // The browser names the frame it is addressing. Absent — an older broker —
  // this endpoint has no browser-space identity to state, and it states none
  // rather than substituting the one it minted for itself.
  browser_frame_id_ = request->observed_frame_id.value_or(std::string());
  sensitivity_policy_id_ = request->sensitivity_policy_id;
  task_purpose_ = request->task_purpose;

  SnapshotBuilder::Input input;
  input.frame = frame_;
  input.store = store_.get();
  input.request = request.get();
  input.browser_facts = FactsFrom(*request);
  // Remembered so the delta path reaches the same answer about the same
  // document rather than recomputing it from facts it does not have.
  cross_origin_frame_ = input.browser_facts.cross_origin_frame;
  input.snapshot_id =
      base::StrCat({"snap-", base::NumberToString(++next_local_id_)});
  input.event_sequence = ++event_sequence_;
  // Reachable only past the kActive guard above, so the optional is engaged.
  input.lifecycle_state = lifecycle_state_;
  allowed_origin_serializations_ =
      input.browser_facts.allowed_origin_serializations;

  SnapshotBuilder::Output built = SnapshotBuilder::Build(
      input, PageCapabilities::ForDocument(
                 CollectCapabilitySignals(frame_, input.browser_facts)));

  result->code = built.code;
  // Only the warnings that have no snapshot to travel on. A build that
  // produced a snapshot put its warnings there, where the contract declares
  // them and where a consumer reads them; this loop then moves nothing.
  for (mojom::SnapshotWarningPtr& warning : built.warnings) {
    result->warnings.push_back(std::move(warning));
  }

  if (built.snapshot) {
    result->snapshot = std::move(built.snapshot);
  }
  std::move(callback).Run(std::move(result));
}

void PageIntelligenceEndpoint::Subscribe(
    mojom::PageSubscriptionOptionsPtr options,
    mojo::PendingRemote<mojom::PageDeltaClient> client,
    SubscribeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto result = mojom::SubscriptionResult::New();
  result->request_id = options->request_id;

  if (invalidated_ || !StoreFor(PageEpoch(options->expected_page_epoch))) {
    result->code = mojom::ObservationResultCode::kStalePageEpoch;
    std::move(callback).Run(std::move(result));
    return;
  }

  negotiated_schema_version_ = options->schema_version;
  // The subscription names the frame too, and has always done so — this is the
  // path the snapshot request was missing an equivalent of.
  browser_frame_id_ = options->frame_id;
  delta_publisher_ = std::make_unique<DeltaPublisher>(
      store_.get(), negotiated_schema_version_, tab_id_, options->frame_id);
  const std::string subscription_id =
      base::StrCat({"sub-", base::NumberToString(++next_local_id_)});
  delta_publisher_->Start(std::move(client),
                          base::Milliseconds(options->coalescing_window_ms),
                          subscription_id);
  if (dom_mutations_) {
    dom_mutations_->SetStreaming(true);
  }

  result->code = mojom::ObservationResultCode::kOk;
  result->subscription_id = subscription_id;
  result->base_revision = store_->current_revision().value();
  std::move(callback).Run(std::move(result));
}

void PageIntelligenceEndpoint::OnDocumentMutated(
    SemanticGraphStore::ChangeClass change,
    SemanticNodeId node_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (invalidated_ || !store_) {
    return;
  }
  const GraphRevision revision = store_->NoteNodeChange(change, node_id);
  if (delta_publisher_) {
    delta_publisher_->OnChange(change, std::move(node_id), revision,
                               &event_sequence_);
  }
}

void PageIntelligenceEndpoint::OnNodesRemoved(
    SemanticGraphStore::IdentitySpace space,
    int64_t source_node_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (invalidated_ || !store_) {
    return;
  }
  for (const SemanticNodeId& retired :
       store_->RetireByDomNode(space, source_node_id)) {
    OnDocumentMutated(SemanticGraphStore::ChangeClass::kNodeRemoved, retired);
  }
}

void PageIntelligenceEndpoint::OnLifecycleChanged(
    mojom::DocumentLifecycleState state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  lifecycle_state_ = state;
  if (state != mojom::DocumentLifecycleState::kActive && delta_publisher_) {
    // Deltas stop the moment the document is not active. A frozen or cached
    // document that kept streaming is how a subscriber ends up applying
    // changes to a projection of a page nobody is looking at.
    delta_publisher_->Flush(&event_sequence_);
  }
}

void PageIntelligenceEndpoint::Invalidate(mojom::InvalidationReason reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  invalidated_ = true;

  // A read waiting for a document that is being torn down is answered here,
  // before anything else is released. `invalidated_` is already set, so the
  // request takes the refusal at the top of BeginSnapshot rather than a second
  // copy of it written out here.
  RunDeferredSnapshot();

  // Stopped before the publisher, not after: every entry point below is gated
  // on `invalidated_`, but the Blink observer is not an entry point of this
  // class and would keep delivering into a document that is being torn down.
  if (dom_mutations_) {
    dom_mutations_->SetStreaming(false);
  }
  dom_mutations_.reset();

  if (delta_publisher_) {
    // The epoch dies with this endpoint, so every handle inside it is dead.
    delta_publisher_->Stop(reason, /*retires_epoch=*/true, &event_sequence_);
    delta_publisher_.reset();
  }

  // The store is kept, not freed: `invalidated_` gates every entry point, and
  // keeping the retired ids means a late handle resolves to a precise refusal
  // rather than to an empty store that would have to guess. The store dies
  // with this endpoint, which dies with its document - so a handle from a
  // retired epoch can never resolve, as a property of the object graph rather
  // than of a comparison someone has to remember to write.
}

}  // namespace taffy
