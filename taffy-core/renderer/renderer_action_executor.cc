// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/renderer_action_executor.h"

#include <algorithm>
#include <optional>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "taffy/renderer/action_accessibility_target.h"
#include "taffy/renderer/adapters/layout_visibility_probe.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/web/web_ax_context.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom-shared.h"
#include "ui/accessibility/ax_mode.h"

// The include list is part of what this file promises. There is no
// web_form_element.h, no web_input_element.h and no web_element.h here, and
// that is not an accident of what happened to be needed: those are the
// headers a direct DOM write would be spelled with, and decision 0059 says
// there is no such write. The only primitive below is
// blink::WebAXObject::PerformAction, and the only thing that decides what to
// hand it is the table in action_accessibility_mapping.h.

// VERIFY AT SP-04:
//   * blink::WebAXObject::FromWebDocumentByID() (or the current spelling) for
//     turning a recorded accessibility id back into an object, and whether it
//     can return an object belonging to a different node after a tree rebuild.
//     If it can, the identity generation in SemanticGraphStore has to be
//     advanced on every accessibility tree rebuild, not only on the recycle
//     signals it advances on today. This is the single most important thing
//     on this file's verification list.
//   * blink::WebAXObject::PerformAction(const ui::AXActionData&) and the
//     action names kScrollToMakeVisible and kFocus.
//   * ax::mojom::Action::kDoDefault semantics at the pin: specifically that
//     it dispatches a real event sequence and does NOT create or extend user
//     activation. If it does either, activation moves to browser-dispatched
//     input and Outcome::kRequiresBrowserInputDispatch becomes the live path.
//   * Whether PerformAction can run script synchronously. If it can, the
//     store must not be mutated across the call, and the barrier has to
//     survive re-entrancy - which it does, being a counter, but the ordering
//     needs a test.

namespace taffy {

RendererActionExecutor::Request::Request() = default;
RendererActionExecutor::Request::Request(const Request&) = default;
RendererActionExecutor::Request::~Request() = default;

RendererActionExecutor::RendererActionExecutor() = default;
RendererActionExecutor::~RendererActionExecutor() = default;

RendererActionExecutor::Result RendererActionExecutor::Execute(
    SemanticGraphStore& store,
    blink::WebLocalFrame* frame,
    const Request& request) {
  Result result;

  // Held across the preflight AND the dispatch. Spec section 5.3: adapters
  // must not coalesce across this boundary, and the barrier is what makes
  // that structural instead of a rule someone has to remember.
  const SemanticGraphStore::ScopedActionBarrier barrier(&store);

  PreconditionRequest precondition = request.precondition;
  precondition.requested_action = request.action;

  // Two requirements this class adds regardless of what the command asked
  // for, because they are properties of the operation rather than of the
  // caller's plan. Adding to the list is safe; the checker only ever refuses
  // more.
  switch (request.action) {
    case ActionKind::kScrollIntoView:
      // Nothing extra: making something visible is the point, so requiring
      // visibility first would be circular.
      break;
    case ActionKind::kFocus:
    case ActionKind::kActivate:
    case ActionKind::kSelectOption:
    case ActionKind::kSubmitForm:
      precondition.required_states.push_back(NodeState::kVisible);
      precondition.required_states.push_back(NodeState::kEnabled);
      break;
    case ActionKind::kToggle:
      precondition.required_states.push_back(NodeState::kVisible);
      precondition.required_states.push_back(NodeState::kEnabled);
      // The accessibility default action FLIPS a control, so the state it
      // must start in is the negation of the state it must end in. Requiring
      // that here rather than comparing afterwards is what makes "we recorded
      // no checked state" a refusal too: the checker satisfies a required
      // state only when the state is asserted, never when it is merely
      // un-negated, so a control nobody could read is refused rather than
      // flipped on a guess.
      if (request.value.checked.has_value()) {
        precondition.required_states.push_back(request.value.checked.value()
                                                   ? NodeState::kUnchecked
                                                   : NodeState::kChecked);
      }
      break;
    case ActionKind::kSetText:
      // A field that is not editable is a refusal rather than an
      // accessibility action that quietly does nothing, and the checker has
      // its own code for it.
      precondition.required_states.push_back(NodeState::kVisible);
      precondition.required_states.push_back(NodeState::kEnabled);
      precondition.required_states.push_back(NodeState::kEditable);
      break;
  }

  // The document facts the checker needs, read here so that the checker
  // itself stays free of Blink and its fail-closed properties stay provable
  // on any host.
  //
  // VERIFY AT SP-04: blink::WebSecurityOrigin::ToString() and IsOpaque() at
  // the pin. An opaque origin must not compare equal to anything.
  DocumentFacts facts;
  if (frame) {
    const blink::WebDocument frame_document = frame->GetDocument();
    facts.has_document = !frame_document.IsNull();
    if (facts.has_document) {
      const blink::WebSecurityOrigin origin =
          frame_document.GetSecurityOrigin();
      facts.origin_is_opaque = origin.IsNull() || origin.IsOpaque();
      if (!facts.origin_is_opaque) {
        facts.origin_serialization = origin.ToString().Utf8();
      }
    }
  }

  // Where the target is now, rather than where the last reading left it: a
  // field read below the fold and scrolled into view since was refused as
  // not visible (decision 0250). Nothing measured leaves the reading's.
  if (frame) {
    if (const SemanticGraphStore::LiveNode* live =
            store.FindLive(precondition.node_id)) {
      facts.visibility_now = MeasureLiveVisibility(frame, live->dom_key);
    }
  }
  {
    const auto now_has = [&facts](NodeState state) {
      return facts.visibility_now.has_value() &&
             std::ranges::find(facts.visibility_now->states, state) !=
                 facts.visibility_now->states.end();
    };
    LOG(WARNING) << "[taffy_action_visibility_now] measured="
                 << facts.visibility_now.has_value()
                 << " visible=" << now_has(NodeState::kVisible)
                 << " offscreen=" << now_has(NodeState::kOffscreen)
                 << " obscured=" << now_has(NodeState::kObscured)
                 << " not_visible=" << now_has(NodeState::kNotVisible);
  }

  const PreconditionResult check =
      checker_.Check(store, barrier, facts, precondition);
  result.precondition = check.code;
  result.revision_at_dispatch = check.observed_revision;
  if (check.code != PreconditionCode::kOk) {
    result.outcome = Outcome::kPreconditionFailed;
    return result;
  }
  result.observed_state = check.node;

  // Decided before the target is resolved, so a malformed command never
  // reaches an element at all. The mapping is total over ActionKind and
  // refuses only when the browser sent an operation without the value it
  // needs, which is a defect on the far side of the boundary.
  const std::optional<AccessibilityAction> mapped =
      AccessibilityActionFor(request.action, request.value);
  if (!mapped.has_value()) {
    result.outcome = Outcome::kUnsupported;
    return result;
  }
  const blink::WebDocument document = frame->GetDocument();

  // A snapshot adapter keeps an accessibility context only for its bounded
  // extraction. Most documents have no other accessibility client, so after
  // that context dies WebAXObject::FromWebNode returns a detached object and
  // every otherwise valid action is refused as NODE_GONE. The operation is
  // defined entirely in terms of Blink's accessibility action path; keep its
  // narrow basic tree alive for the complete lookup-and-dispatch interval and
  // refresh it before resolving the exact node.
  blink::WebAXContext ax_context(document, ui::kAXModeBasic);
  if (!ax_context.HasActiveDocument()) {
    result.outcome = Outcome::kDispatchFailed;
    return result;
  }
  ax_context.UpdateAXForAllDocuments();
  const ResolvedAccessibilityTarget target =
      ResolveAccessibilityTarget(document, check.node->dom_key, mapped.value());
  if (target.status == AccessibilityTargetStatus::kUnsupported) {
    result.outcome = Outcome::kUnsupported;
    return result;
  }
  if (target.status != AccessibilityTargetStatus::kOk ||
      target.object.IsDetached()) {
    // The node was there one statement ago. This is the race the barrier
    // exists to make visible; report it rather than retrying.
    result.outcome = Outcome::kDispatchFailed;
    return result;
  }

  // One primitive, for every operation. There is no second branch here and
  // there is deliberately nowhere to add one: what varies between a scroll
  // and a form submission is which ax::mojom::Action the table returned, not
  // which Blink API this class reaches for.
  ui::AXActionData action_data;
  action_data.action = mapped->action;
  action_data.value = mapped->value;

  // A successful accessibility API return is only DISPATCHED. For a value
  // write, the exact node must also emit the page's normal input event before
  // NODE_VALUE_CHANGED can ever verify. Listen only for the duration of this
  // accessibility action, and carry only the boolean event fact into the
  // graph: no value, length, mask or digest is read back from the page.
  bool input_event_observed = false;
  base::ScopedClosureRunner remove_input_listener;
  if ((request.action == ActionKind::kSetText ||
       request.action == ActionKind::kSelectOption) &&
      check.node->dom_key.space == SemanticGraphStore::IdentitySpace::kDom) {
    blink::WebNode event_target = blink::WebNode::FromDomNodeId(
        static_cast<int>(check.node->dom_key.dom_node_id));
    if (!event_target.IsNull() && event_target.GetDocument() == document) {
      remove_input_listener = event_target.AddEventListener(
          blink::WebNode::EventType::kInput,
          base::BindRepeating(
              [](bool* observed, blink::WebDOMEvent) { *observed = true; },
              base::Unretained(&input_event_observed)));
    }
  }

  if (!target.object.PerformAction(action_data)) {
    result.outcome = Outcome::kDispatchFailed;
    return result;
  }

  // The action moved the page, so the graph moved with it. A mutating
  // operation is the stronger case: it may have changed a value or started a
  // navigation, and the browser will see the navigation for itself and turn
  // it into an invalidation.
  if (input_event_observed) {
    store.NoteNodeChange(SemanticGraphStore::ChangeClass::kFormStateChanged,
                         check.node->node_id);
  } else {
    store.NoteChange(ActionMutatesPage(request.action)
                         ? SemanticGraphStore::ChangeClass::kAdapterInvalidated
                         : SemanticGraphStore::ChangeClass::kVisibilityChanged);
  }
  // `revision_at_dispatch` is the last graph revision known before the
  // accessibility action crossed into the page. A synchronous input event
  // above may already have advanced the store and the exact node's
  // value-change revision. Replacing the baseline with that post-action
  // revision makes the browser's deliberately strict `evidence > dispatch`
  // check impossible to satisfy: both numbers would name the same event.
  // Keep the preflight revision recorded above; a later ResolveNode then
  // independently observes the newer store state.
  result.outcome = Outcome::kPerformed;
  return result;
}

}  // namespace taffy
