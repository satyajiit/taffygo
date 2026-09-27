// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/node_precondition_checker.h"

#include <algorithm>
#include <vector>

#include "base/logging.h"

#include "taffy/renderer/field_redaction.h"

namespace taffy {

namespace {

bool HasAction(const std::vector<ActionKind>& actions, ActionKind action) {
  return std::ranges::find(actions, action) != actions.end();
}

bool HasState(const std::vector<NodeState>& states, NodeState state) {
  return std::ranges::find(states, state) != states.end();
}

}  // namespace

PreconditionRequest::PreconditionRequest() = default;
PreconditionRequest::PreconditionRequest(const PreconditionRequest&) = default;
PreconditionRequest::PreconditionRequest(PreconditionRequest&&) = default;
PreconditionRequest& PreconditionRequest::operator=(
    const PreconditionRequest&) = default;
PreconditionRequest& PreconditionRequest::operator=(PreconditionRequest&&) =
    default;
PreconditionRequest::~PreconditionRequest() = default;

PreconditionResult::PreconditionResult() = default;
PreconditionResult::PreconditionResult(const PreconditionResult&) = default;
PreconditionResult::~PreconditionResult() = default;

NodePreconditionChecker::NodePreconditionChecker() = default;
NodePreconditionChecker::~NodePreconditionChecker() = default;

MeasuredVisibility::MeasuredVisibility() = default;
MeasuredVisibility::MeasuredVisibility(const MeasuredVisibility&) = default;
MeasuredVisibility& MeasuredVisibility::operator=(const MeasuredVisibility&) =
    default;
MeasuredVisibility::~MeasuredVisibility() = default;

bool IsVisibilityState(NodeState state) {
  return state == NodeState::kVisible || state == NodeState::kNotVisible ||
         state == NodeState::kOffscreen || state == NodeState::kObscured;
}

DocumentFacts::DocumentFacts() = default;
DocumentFacts::DocumentFacts(const DocumentFacts&) = default;
DocumentFacts& DocumentFacts::operator=(const DocumentFacts&) = default;
DocumentFacts::~DocumentFacts() = default;

PreconditionResult NodePreconditionChecker::Check(
    const SemanticGraphStore& store,
    const SemanticGraphStore::ScopedActionBarrier& barrier,
    const DocumentFacts& document,
    const PreconditionRequest& request) const {
  PreconditionResult result;
  result.observed_revision = store.current_revision();

  // The result starts as a refusal and is only ever narrowed to kOk by the
  // last statement. An early return that forgets to set a code therefore
  // refuses, which is the direction a mistake should fail in.
  result.code = PreconditionCode::kUnsupported;

  if (!document.has_document) {
    return result;
  }
  if (request.expected_origin_serialization.has_value()) {
    // An opaque document never satisfies this check: every opaque origin
    // serializes to the same token, so string equality would report a match
    // between two unrelated documents. The browser compares nonces and that
    // comparison is the one that means anything.
    if (document.origin_is_opaque ||
        document.origin_serialization !=
            request.expected_origin_serialization.value()) {
      result.code = PreconditionCode::kOriginChanged;
      return result;
    }
  }

  // Spec section 12 step 5: re-resolve the non-reused node id at the required
  // graph revision. Not "find the node that looks like it"; resolve the id or
  // refuse.
  const SemanticGraphStore::ResolveResult resolved =
      store.Resolve(request.node_id, request.expected_page_epoch,
                    request.minimum_graph_revision);
  switch (resolved.status) {
    case SemanticGraphStore::ResolveStatus::kStalePageEpoch:
      result.code = PreconditionCode::kStalePageEpoch;
      return result;
    case SemanticGraphStore::ResolveStatus::kNodeGone:
    case SemanticGraphStore::ResolveStatus::kNodeUnknown:
      // Both are kNodeGone to the caller. The caller's only correct response
      // is a fresh observation, and telling it apart would tempt someone into
      // a retry path that protocol section 12 forbids.
      result.code = PreconditionCode::kNodeGone;
      return result;
    case SemanticGraphStore::ResolveStatus::kStaleGraph:
      // Which of the two freshness answers this was, in counters only: a
      // store behind the floor it was given, or the node itself having
      // changed after it. Both reach the browser as one code, and a device
      // run could not tell them apart (decision 0188).
      LOG(WARNING) << "[taffy_node_stale]"
                   << " floor_ahead="
                   << (request.minimum_graph_revision.has_value() &&
                               *request.minimum_graph_revision >
                                   resolved.current_revision
                           ? 1
                           : 0)
                   << " node_changed_after_floor="
                   << (resolved.node_changed_at.value() != 0u &&
                               request.minimum_graph_revision.has_value()
                           ? resolved.node_changed_at.value() -
                                 request.minimum_graph_revision->value()
                           : 0u)
                   << " page_ahead_of_floor="
                   << (request.minimum_graph_revision.has_value() &&
                               resolved.current_revision >
                                   *request.minimum_graph_revision
                           ? resolved.current_revision.value() -
                                 request.minimum_graph_revision->value()
                           : 0u);
      result.code = PreconditionCode::kStaleGraph;
      return result;
    case SemanticGraphStore::ResolveStatus::kOk:
      break;
  }
  if (!resolved.node.has_value()) {
    return result;
  }
  const SemanticGraphStore::LiveNode& node = resolved.node.value();

  // The page moved while we were checking it. The node may still look right,
  // but something else changed in this epoch, and the whole point of the
  // barrier is that this is visible rather than coalesced away.
  if (barrier.graph_moved()) {
    result.code = PreconditionCode::kGraphMovedDuringPreflight;
    return result;
  }

  // Spec section 12 step 6, in the order that fails cheapest first.
  if (request.expected_role.has_value() &&
      node.role != request.expected_role.value()) {
    result.code = PreconditionCode::kRoleOrActionChanged;
    return result;
  }
  if (!HasAction(node.actions, request.requested_action)) {
    result.code = PreconditionCode::kRoleOrActionChanged;
    return result;
  }

  // Sensitivity is checked before anything that could touch the node, and it
  // is checked against the classification we recorded, not recomputed here: a
  // recomputation could disagree with what the browser process authorized
  // against, and the safe direction is to refuse on the recorded value.
  const Sensitivity ceiling =
      request.max_sensitivity.value_or(Sensitivity::kNotSensitive);
  if (SensitivityClassifier::Stricter(node.sensitivity, ceiling) !=
      ceiling) {
    result.code = PreconditionCode::kSensitiveField;
    return result;
  }

  if (request.content_trust_check_declared &&
      (node.content_trust == RendererContentTrust::kUnknownUntrusted ||
       (request.forbidden_content_trust &&
        node.content_trust == *request.forbidden_content_trust))) {
    // The browser already made the policy decision. This independent check
    // can only refuse if authorship changed before the accessibility action.
    result.code = PreconditionCode::kUnsupported;
    return result;
  }

  // What visibility is judged on: the reading's states, with their visibility
  // replaced by where the node is now when that was measured (decision 0250).
  std::vector<NodeState> states = node.states;
  bool occlusion_determined = node.occlusion_determined;
  if (document.visibility_now.has_value()) {
    std::erase_if(states, IsVisibilityState);
    states.insert(states.end(), document.visibility_now->states.begin(),
                  document.visibility_now->states.end());
    occlusion_determined = document.visibility_now->occlusion_determined;
  }

  // Asserted, not merely un-negated: both a state and its negation exist, so
  // silence means "could not tell" and does not satisfy a requirement.
  for (NodeState required : request.required_states) {
    if (HasState(states, required)) {
      continue;
    }
    switch (required) {
      case NodeState::kVisible:
        result.code = PreconditionCode::kNotVisible;
        return result;
      case NodeState::kEnabled:
        result.code = PreconditionCode::kNotEnabled;
        return result;
      case NodeState::kEditable:
        result.code = PreconditionCode::kNotEditable;
        return result;
      default:
        // A state this endpoint records but has no specific refusal code for.
        // Unsupported is still a refusal.
        result.code = PreconditionCode::kUnsupported;
        return result;
    }
  }

  // A forbidden state is checked in two halves, and the second half is the
  // one that used to be missing.
  //
  // The obvious half: if the node ASSERTS the state, refuse.
  //
  // The half that matters: for occlusion, silence is not a pass. Both
  // mojom::NodeState members exist so that "not asserted" stays
  // distinguishable from "asserted false" - and an adapter that never probed
  // asserts neither. Treating that silence as "not obscured" would mean a
  // caller could get an action past a visibility gate simply by observing a
  // document where the probe budget ran out. So a caller that forbids
  // kObscured also requires a POSITIVE determination, and gets a refusal when
  // there is none.
  //
  // Whether any renderer occlusion test is reliable enough to gate a
  // consequential action on is `[Open (OD-054)]`. Until that is settled this
  // check is what stops the endpoint from implying an answer it does not
  // have, and the browser process re-checks what it can see for itself
  // regardless (protocol section 11.4).
  if (std::ranges::find(request.forbidden_states, NodeState::kObscured) !=
          request.forbidden_states.end() &&
      !occlusion_determined) {
    result.code = PreconditionCode::kUnsupported;
    return result;
  }

  for (NodeState forbidden : request.forbidden_states) {
    if (!HasState(states, forbidden)) {
      continue;
    }
    switch (forbidden) {
      case NodeState::kObscured:
        result.code = PreconditionCode::kOccluded;
        return result;
      case NodeState::kNotVisible:
      case NodeState::kOffscreen:
        result.code = PreconditionCode::kNotVisible;
        return result;
      case NodeState::kDisabled:
        result.code = PreconditionCode::kNotEnabled;
        return result;
      case NodeState::kReadOnly:
        result.code = PreconditionCode::kNotEditable;
        return result;
      default:
        result.code = PreconditionCode::kUnsupported;
        return result;
    }
  }

  // Bounds are compared, never resolved from. A target that MOVED between
  // observation and dispatch has changed a precondition the caller reasoned
  // about even when its role, states, and destination are all unchanged -
  // the classic shape being a consent dialog sliding a different button
  // under the point the plan was made about. Protocol section 12 forbids
  // finding a node by "nearest coordinates", and nothing here does: the id
  // was already resolved, and this only asks whether what it resolved to is
  // still where it was.
  if (request.expected_bounds.has_value()) {
    if (!node.bounds.has_value() ||
        node.bounds->x != request.expected_bounds->x ||
        node.bounds->y != request.expected_bounds->y ||
        node.bounds->width != request.expected_bounds->width ||
        node.bounds->height != request.expected_bounds->height) {
      result.code = PreconditionCode::kNotVisible;
      return result;
    }
  }

  // Destination is compared literally against what was observed. A page that
  // swapped an href after the plan was made is the single most valuable
  // attack against an assistant that clicks links, and "close enough" has no
  // meaning here.
  if (request.expected_destination.has_value() &&
      (!node.destination.has_value() ||
       node.destination->url != request.expected_destination.value())) {
    result.code = PreconditionCode::kDestinationChanged;
    return result;
  }

  // One last look at the barrier: everything above took time, and the page is
  // allowed to change during it.
  if (barrier.graph_moved()) {
    result.code = PreconditionCode::kGraphMovedDuringPreflight;
    return result;
  }

  result.code = PreconditionCode::kOk;
  result.node = node;
  return result;
}

}  // namespace taffy
