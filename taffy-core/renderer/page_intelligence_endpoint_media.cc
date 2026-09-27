// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <utility>

#include "taffy/renderer/page_intelligence_endpoint.h"
#include "taffy/renderer/page_media_caption_cues.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_frame_widget.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "ui/gfx/geometry/rect.h"

namespace taffy {
namespace {

constexpr size_t kMaximumVisualRedactionBounds = 128u;

mojom::BoundsPtr ToMojomBounds(const NodeBounds& bounds) {
  auto out = mojom::Bounds::New();
  out->x = bounds.x;
  out->y = bounds.y;
  out->width = bounds.width;
  out->height = bounds.height;
  return out;
}

}  // namespace

void PageIntelligenceEndpoint::InspectMediaTarget(
    mojom::MediaTargetRequestPtr request,
    InspectMediaTargetCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto result = mojom::MediaTargetResult::New();
  result->request_id = request->request_id;
  result->kind = request->expected_kind;
  if (invalidated_ || !store_ ||
      lifecycle_state_ != mojom::DocumentLifecycleState::kActive) {
    result->code = mojom::MediaTargetResultCode::kDocumentInactive;
    std::move(callback).Run(std::move(result));
    return;
  }
  if (request->expected_kind == mojom::MediaTargetKind::kPageScreenshot) {
    result->observed_graph_revision = store_->current_revision().value();
    if (store_->page_epoch() != PageEpoch(request->expected_page_epoch)) {
      result->code = mojom::MediaTargetResultCode::kStalePageEpoch;
      std::move(callback).Run(std::move(result));
      return;
    }
    if (store_->current_revision() <
        GraphRevision(request->required_graph_revision)) {
      result->code = mojom::MediaTargetResultCode::kStaleGraph;
      std::move(callback).Run(std::move(result));
      return;
    }
    if (!request->node_id.empty() || request->include_loaded_caption_cues) {
      result->code = mojom::MediaTargetResultCode::kWrongType;
      std::move(callback).Run(std::move(result));
      return;
    }
    const std::optional<std::vector<int64_t>> redaction_nodes =
        store_->VisualRedactionDomNodeIds(kMaximumVisualRedactionBounds);
    blink::WebFrameWidget* widget = frame_->FrameWidget();
    const gfx::Size viewport = widget ? widget->Size() : gfx::Size();
    if (!redaction_nodes) {
      result->code = mojom::MediaTargetResultCode::kUnsafeContent;
      std::move(callback).Run(std::move(result));
      return;
    }
    if (viewport.IsEmpty()) {
      result->code = mojom::MediaTargetResultCode::kEmptyBounds;
      std::move(callback).Run(std::move(result));
      return;
    }
    // Measured into a local and handed over only once every prohibited region
    // has been accounted for. `result->bounds` used to be filled in above this
    // loop, so each of the three refusals below returned carrying the viewport
    // geometry it was in the middle of refusing to authorize. A result that
    // says "unsafe" and hands over bounds anyway invites a caller to read the
    // second and act on it, and the caller most likely to do that is one
    // written later by somebody who trusted the field's presence.
    std::vector<mojom::BoundsPtr> redaction_bounds;
    const gfx::Rect visible_viewport(viewport);
    const blink::WebDocument document = frame_->GetDocument();
    for (int64_t dom_node_id : *redaction_nodes) {
      if (dom_node_id <= 0 ||
          dom_node_id > std::numeric_limits<int>::max()) {
        result->code = mojom::MediaTargetResultCode::kUnsafeContent;
        std::move(callback).Run(std::move(result));
        return;
      }
      const blink::WebNode node = blink::WebNode::FromDomNodeId(
          static_cast<int>(dom_node_id));
      if (document.IsNull() || node.IsNull() || !node.IsConnected() ||
          node.GetDocument() != document || !node.IsElementNode()) {
        result->code = mojom::MediaTargetResultCode::kUnsafeContent;
        std::move(callback).Run(std::move(result));
        return;
      }
      gfx::Rect visible = node.To<blink::WebElement>().VisibleBoundsInWidget();
      if (visible.IsEmpty()) {
        // A prohibited secret that is still in the document and has no
        // measurable geometry — hidden, collapsed, or not laid out yet. It is
        // the contract's own first example of unsafe content, and refusing is
        // the whole point: the alternative is a capture with an unredactable
        // secret somewhere in it.
        result->code = mojom::MediaTargetResultCode::kUnsafeContent;
        std::move(callback).Run(std::move(result));
        return;
      }
      visible.Intersect(visible_viewport);
      if (!visible.IsEmpty()) {
        redaction_bounds.push_back(ToMojomBounds(NodeBounds{
            .x = visible.x(),
            .y = visible.y(),
            .width = visible.width(),
            .height = visible.height(),
        }));
      }
    }
    result->bounds = mojom::Bounds::New();
    result->bounds->width = viewport.width();
    result->bounds->height = viewport.height();
    result->redaction_bounds = std::move(redaction_bounds);
    result->code = mojom::MediaTargetResultCode::kOk;
    std::move(callback).Run(std::move(result));
    return;
  }
  const SemanticGraphStore::ResolveResult resolved = store_->Resolve(
      SemanticNodeId(request->node_id), PageEpoch(request->expected_page_epoch),
      GraphRevision(request->required_graph_revision));
  result->observed_graph_revision = resolved.current_revision.value();
  switch (resolved.status) {
    case SemanticGraphStore::ResolveStatus::kStalePageEpoch:
      result->code = mojom::MediaTargetResultCode::kStalePageEpoch;
      std::move(callback).Run(std::move(result));
      return;
    case SemanticGraphStore::ResolveStatus::kStaleGraph:
      result->code = mojom::MediaTargetResultCode::kStaleGraph;
      std::move(callback).Run(std::move(result));
      return;
    case SemanticGraphStore::ResolveStatus::kNodeGone:
    case SemanticGraphStore::ResolveStatus::kNodeUnknown:
      result->code = mojom::MediaTargetResultCode::kNodeGone;
      std::move(callback).Run(std::move(result));
      return;
    case SemanticGraphStore::ResolveStatus::kOk:
      break;
  }
  if (!resolved.node ||
      resolved.node->dom_key.space != SemanticGraphStore::IdentitySpace::kDom) {
    result->code = mojom::MediaTargetResultCode::kWrongType;
    std::move(callback).Run(std::move(result));
    return;
  }
  const bool wants_image =
      request->expected_kind == mojom::MediaTargetKind::kImage;
  if ((wants_image && resolved.node->role != SemanticRole::kImage) ||
      (!wants_image && resolved.node->role != SemanticRole::kMedia)) {
    result->code = mojom::MediaTargetResultCode::kWrongType;
    std::move(callback).Run(std::move(result));
    return;
  }
  // The person's challenge image and the model's image attachment share the
  // compositor, so the renderer must keep the authority split before pixels
  // exist. Re-check on both the pre- and post-capture inspections: a form that
  // becomes a challenge while CopyFromSurface is in flight is refused too.
  if (wants_image && store_->IsChallengePresentationTarget(
                         resolved.node->dom_key.dom_node_id,
                         ChallengeKind::kImage)) {
    result->code = mojom::MediaTargetResultCode::kWrongType;
    std::move(callback).Run(std::move(result));
    return;
  }
  const blink::WebDocument document = frame_->GetDocument();
  const blink::WebNode node = blink::WebNode::FromDomNodeId(
      static_cast<int>(resolved.node->dom_key.dom_node_id));
  if (document.IsNull() || node.IsNull() || !node.IsConnected() ||
      node.GetDocument() != document || !node.IsElementNode()) {
    result->code = mojom::MediaTargetResultCode::kWrongType;
    std::move(callback).Run(std::move(result));
    return;
  }
  const blink::WebElement element = node.To<blink::WebElement>();
  const blink::WebString expected_tag =
      blink::WebString::FromUtf8(wants_image ? "img" : "video");
  if (element.IsNull() || !element.HasHTMLTagName(expected_tag)) {
    result->code = mojom::MediaTargetResultCode::kWrongType;
    std::move(callback).Run(std::move(result));
    return;
  }
  const gfx::Rect bounds = element.VisibleBoundsInWidget();
  if (bounds.IsEmpty()) {
    result->code = mojom::MediaTargetResultCode::kEmptyBounds;
    std::move(callback).Run(std::move(result));
    return;
  }
  result->bounds = mojom::Bounds::New();
  result->bounds->x = bounds.x();
  result->bounds->y = bounds.y();
  result->bounds->width = bounds.width();
  result->bounds->height = bounds.height();
  if (!wants_image && request->include_loaded_caption_cues) {
    result->loaded_caption_cues =
        ExtractLoadedCaptionCues(frame_, std::move(element));
  }
  result->code = mojom::MediaTargetResultCode::kOk;
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy
