// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "taffy/renderer/adapters/layout_visibility_probe.h"
#include "taffy/renderer/page_intelligence_endpoint.h"
#include "taffy/renderer/wire_conversions.h"
#include "third_party/blink/public/web/web_ax_context.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_frame_widget.h"
#include "third_party/blink/public/web/web_input_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "ui/accessibility/ax_enums.mojom-shared.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"

namespace taffy {
namespace {

void RefreshCheckedState(const SemanticGraphStore::LiveNode& node,
                         blink::WebLocalFrame* frame,
                         std::vector<NodeState>* states) {
  if (!frame || !states ||
      (node.role != SemanticRole::kCheckbox &&
       node.role != SemanticRole::kRadio)) {
    return;
  }
  const blink::WebDocument document = frame->GetDocument();
  if (document.IsNull()) {
    return;
  }
  // ResolveNode can run after every snapshot-owned accessibility context has
  // gone away. In that state FromWebNode returns a detached object and the
  // verifier keeps the stale pre-action checked state until timeout. Hold the
  // same narrow basic context used by extraction and action dispatch for the
  // complete live re-read.
  blink::WebAXContext ax_context(document, ui::kAXModeBasic);
  if (!ax_context.HasActiveDocument()) {
    return;
  }
  ax_context.UpdateAXForAllDocuments();
  blink::WebAXObject object;
  // Kept beyond the switch so the fallback below can ask the element itself.
  // Only a DOM-space node has one to ask; an accessibility-space node is named
  // by a tree identifier and has no element behind it here.
  blink::WebNode dom_node;
  switch (node.dom_key.space) {
    case SemanticGraphStore::IdentitySpace::kAccessibility:
      object = blink::WebAXObject::FromWebDocumentByID(
          document, static_cast<int>(node.dom_key.dom_node_id));
      break;
    case SemanticGraphStore::IdentitySpace::kDom: {
      dom_node = blink::WebNode::FromDomNodeId(
          static_cast<int>(node.dom_key.dom_node_id));
      if (dom_node.IsNull() || dom_node.GetDocument() != document) {
        return;
      }
      object = blink::WebAXObject::FromWebNode(dom_node);
      break;
    }
    case SemanticGraphStore::IdentitySpace::kDerived:
      return;
  }
  if (object.IsDetached() || !object.IsIncludedInTree()) {
    // Blink refuses to serialize an object it did not include in its tree, and
    // refuses it fatally outside an official build. The identifier resolved
    // above names a node the browser asked about, which need not be one the
    // accessibility tree exposes.
    //
    // Return before the erase below, as the detached case does: the states
    // already recorded are what the snapshot observed, and an object that
    // cannot be serialized is not evidence that they have changed.
    return;
  }
  ui::AXNodeData data;
  object.Serialize(&data, ui::kAXModeBasic);
  std::optional<NodeState> refreshed;
  switch (data.GetCheckedState()) {
    case ax::mojom::CheckedState::kTrue:
      refreshed = NodeState::kChecked;
      break;
    case ax::mojom::CheckedState::kFalse:
      refreshed = NodeState::kUnchecked;
      break;
    case ax::mojom::CheckedState::kMixed:
      refreshed = NodeState::kMixed;
      break;
    case ax::mojom::CheckedState::kNone:
      break;
  }

  // The tree answering kNone is the tree declining to say, and this function
  // runs at the one moment that distinction is expensive: a verifier reads it
  // immediately after a toggle to confirm the end state it asked for. So ask
  // the element, exactly as the form adapter does when it builds the baseline
  // this refreshes. The element has no indeterminate accessor, so kMixed is
  // reachable only through the tree above.
  if (!refreshed && !dom_node.IsNull()) {
    const blink::WebInputElement input =
        dom_node.DynamicTo<blink::WebInputElement>();
    if (!input.IsNull()) {
      refreshed = input.IsChecked() ? NodeState::kChecked
                                    : NodeState::kUnchecked;
    }
  }

  // The erase moved below the read on purpose. It used to run first and
  // unconditionally, so a tree that answered kNone left the node carrying no
  // checked state at all — the snapshot had recorded one correctly and this
  // deleted it, which is worse than not refreshing. A refresh that has nothing
  // to say must leave what it found; the states already recorded are what the
  // snapshot observed, and silence is not evidence they changed.
  if (!refreshed) {
    return;
  }
  std::erase_if(*states, [](NodeState state) {
    return state == NodeState::kChecked || state == NodeState::kUnchecked ||
           state == NodeState::kMixed;
  });
  states->push_back(*refreshed);
}

}  // namespace

void PageIntelligenceEndpoint::ResolveNode(mojom::ResolveNodeRequestPtr request,
                                           ResolveNodeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto result = mojom::ResolveNodeResult::New();
  result->request_id = request->request_id;

  if (invalidated_ || !store_ ||
      lifecycle_state_ != mojom::DocumentLifecycleState::kActive) {
    result->code = mojom::NodeResolutionCode::kDocumentInactive;
    std::move(callback).Run(std::move(result));
    return;
  }

  const SemanticGraphStore::ResolveResult resolved =
      store_->Resolve(SemanticNodeId(request->node_handle->node_id),
                      PageEpoch(request->node_handle->page_epoch),
                      GraphRevision(request->required_graph_revision));

  switch (resolved.status) {
    case SemanticGraphStore::ResolveStatus::kStalePageEpoch:
      result->code = mojom::NodeResolutionCode::kStalePageEpoch;
      break;
    case SemanticGraphStore::ResolveStatus::kStaleGraph:
      result->code = mojom::NodeResolutionCode::kStaleGraph;
      break;
    case SemanticGraphStore::ResolveStatus::kNodeGone:
    case SemanticGraphStore::ResolveStatus::kNodeUnknown:
      // Both are kNodeGone to the caller. The only correct response to either
      // is a fresh observation, and telling them apart here would tempt
      // someone into the retry path protocol section 12 forbids.
      result->code = mojom::NodeResolutionCode::kNodeGone;
      break;
    case SemanticGraphStore::ResolveStatus::kOk:
      result->node =
          BuildResolvedNode(resolved.node.value(), resolved.current_revision,
                            /*refresh_live_checked_state=*/true);
      result->code = mojom::NodeResolutionCode::kOk;
      break;
  }
  std::move(callback).Run(std::move(result));
}

mojom::ResolvedNodePtr PageIntelligenceEndpoint::BuildResolvedNode(
    const SemanticGraphStore::LiveNode& node,
    GraphRevision revision,
    bool refresh_live_checked_state) const {
  auto out = mojom::ResolvedNode::New();
  out->node_id = node.node_id.value();
  out->display_label = node.display_label;
  out->observed_at_revision = revision.value();
  out->role = wire::ToMojom(node.role);
  for (ActionKind action : node.actions) {
    out->actions.push_back(wire::ToMojom(action));
  }
  std::vector<NodeState> states = node.states;
  if (refresh_live_checked_state) {
    RefreshCheckedState(node, frame_, &states);
    // Where the node is now. A verifier asking whether a scroll brought a
    // line into view read the reading's "off screen" until its deadline,
    // because no reading follows a scroll (decision 0250). Silence leaves
    // the reading's states, as the checked-state refresh does.
    if (const std::optional<MeasuredVisibility> now =
            MeasureLiveVisibility(frame_, node.dom_key)) {
      std::erase_if(states, IsVisibilityState);
      states.insert(states.end(), now->states.begin(), now->states.end());
    }
  }
  for (NodeState state : states) {
    out->states.push_back(wire::ToMojom(state));
  }
  out->sensitivity = wire::ToMojom(node.sensitivity);
  out->content_trust = wire::ToMojom(node.content_trust);
  out->challenge_kind = wire::ToMojom(node.challenge_kind);
  out->value_changed_at_revision = node.value_changed_at_revision.value();
  if (node.challenge_dom_node_id.has_value()) {
    const blink::WebDocument document = frame_->GetDocument();
    const blink::WebNode challenge_node = blink::WebNode::FromDomNodeId(
        static_cast<int>(*node.challenge_dom_node_id));
    if (!document.IsNull() && !challenge_node.IsNull() &&
        challenge_node.IsConnected() &&
        challenge_node.GetDocument() == document &&
        challenge_node.IsElementNode()) {
      // Blink measures in physical pixels, and the browser clips and copies
      // in device-independent ones. Sent unconverted, a CAPTCHA two thirds of
      // the way down a phone's screen fell outside a viewport 3.25 times too
      // small, and was refused as off screen while the person could see it
      // (decision 0247). Rounded outward, so the picture is never cut short.
      blink::WebFrameWidget* widget = frame_->LocalRoot()->FrameWidget();
      const gfx::RectF in_widget(
          challenge_node.To<blink::WebElement>().VisibleBoundsInWidget());
      const gfx::Rect bounds =
          widget ? gfx::ToEnclosingRect(widget->BlinkSpaceToDIPs(in_widget))
                 : gfx::Rect();
      if (bounds.IsEmpty()) {
        // The element the classifier named has no visible box right now —
        // scrolled away, not laid out, or hidden. Named because the browser
        // cannot tell this apart from "no element was named", and the two have
        // different fixes: this one is a scroll, and that one is the
        // classifier (decision 0199).
        LOG(WARNING) << "[taffy_challenge_target_not_visible]";
      } else {
        out->challenge_bounds = mojom::Bounds::New(
            bounds.x(), bounds.y(), bounds.width(), bounds.height());
      }
    } else {
      LOG(WARNING) << "[taffy_challenge_target_unresolvable]";
    }
  } else if (node.challenge_kind == ChallengeKind::kImage ||
             node.challenge_kind == ChallengeKind::kInteractive) {
    // A drawable challenge whose classifier named no element to draw. The
    // sheet cannot open for this field and the browser's own line says only
    // that there was no picture (decision 0199).
    LOG(WARNING) << "[taffy_challenge_target_unnamed] kind="
                 << static_cast<int>(node.challenge_kind);
  }
  if (node.form_field_node_ids.has_value()) {
    for (const SemanticNodeId& field : *node.form_field_node_ids) {
      out->form_field_node_ids.push_back(field.value());
    }
  }
  if (node.destination.has_value()) {
    out->destination = wire::ToMojom(node.destination.value());
  }
  // Ordinary node bounds remain snapshot-only diagnostic hints. Challenge
  // bounds are a separately identified visual target and remain a hint: the
  // browser clips and re-resolves them and never turns geometry into action
  // identity.
  // The value digest remains absent because no adapter reads a value here,
  // especially not a secret value.
  return out;
}

}  // namespace taffy
