// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/semantic_graph_store.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "taffy/renderer/content_metadata.h"

namespace taffy {
namespace {

bool SameDestination(const std::optional<Destination>& a,
                     const std::optional<Destination>& b) {
  if (a.has_value() != b.has_value()) {
    return false;
  }
  if (!a.has_value()) {
    return true;
  }
  return a->url == b->url && a->is_cross_origin == b->is_cross_origin &&
         a->opens_new_tab == b->opens_new_tab &&
         a->is_download == b->is_download;
}

bool SameBounds(const std::optional<NodeBounds>& a,
                const std::optional<NodeBounds>& b) {
  if (a.has_value() != b.has_value()) {
    return false;
  }
  if (!a.has_value()) {
    return true;
  }
  return a->x == b->x && a->y == b->y && a->width == b->width &&
         a->height == b->height;
}

template <typename T>
void Canonicalize(std::vector<T>& values) {
  std::ranges::sort(values);
  values.erase(std::ranges::unique(values).begin(), values.end());
}

bool IsFormOwnedState(NodeState state) {
  switch (state) {
    case NodeState::kEnabled:
    case NodeState::kDisabled:
    case NodeState::kEditable:
    case NodeState::kReadOnly:
    case NodeState::kRequired:
    case NodeState::kInvalid:
    case NodeState::kChecked:
    case NodeState::kUnchecked:
    case NodeState::kMixed:
      return true;
    case NodeState::kVisible:
    case NodeState::kNotVisible:
    case NodeState::kOffscreen:
    case NodeState::kObscured:
    case NodeState::kSelected:
    case NodeState::kExpanded:
    case NodeState::kCollapsed:
    case NodeState::kFocused:
    case NodeState::kBusy:
      return false;
  }
  return false;
}

bool PreconditionRelevantChange(const SemanticGraphStore::LiveNode& before,
                                const SemanticGraphStore::LiveNode& after) {
  return before.display_label != after.display_label ||
         before.role != after.role || before.actions != after.actions ||
         before.sensitivity != after.sensitivity ||
         before.content_trust != after.content_trust ||
         before.challenge_kind != after.challenge_kind ||
         before.challenge_dom_node_id != after.challenge_dom_node_id ||
         before.form_field_node_ids != after.form_field_node_ids ||
         before.states != after.states ||
         before.occlusion_determined != after.occlusion_determined ||
         !SameBounds(before.bounds, after.bounds) ||
         !SameDestination(before.destination, after.destination);
}

}  // namespace

void SemanticGraphStore::UpsertLiveNode(LiveNode node) {
  CHECK(!IsRetired(node.node_id));

  // The stored form of `actions` and `states` is canonical. Two describers
  // may know the same members in different orders, and reordering is not a
  // precondition-relevant change.
  Canonicalize(node.actions);
  Canonicalize(node.states);

  auto it = live_by_id_.find(node.node_id);
  if (it == live_by_id_.end()) {
    node.last_changed = current_revision_;
    live_by_id_.emplace(node.node_id, std::move(node));
    return;
  }

  // Form-schema descriptions are exact for form-owned facts. Generic DOM or
  // accessibility descriptions must not clear those facts merely because
  // they cannot reconstruct them.
  const bool incoming_has_exact_form_semantics =
      node.form_semantics_authoritative;
  const bool preserve_stored_form_semantics =
      it->second.form_semantics_authoritative &&
      !incoming_has_exact_form_semantics;
  if (preserve_stored_form_semantics) {
    node.display_label = it->second.display_label;
    node.role = it->second.role;
    node.actions = it->second.actions;
    node.sensitivity = it->second.sensitivity;
    node.challenge_kind = it->second.challenge_kind;
    node.challenge_dom_node_id = it->second.challenge_dom_node_id;
    node.form_field_node_ids = it->second.form_field_node_ids;
    node.destination = it->second.destination;
    node.form_semantics_authoritative = true;
  }

  // Layout, visibility and focus annotations have independent producers.
  // Carry them across descriptions, except when an exact form re-read is
  // allowed to clear an old form-owned state.
  for (NodeState state : it->second.states) {
    if (incoming_has_exact_form_semantics && IsFormOwnedState(state)) {
      continue;
    }
    if (std::ranges::find(node.states, state) == node.states.end()) {
      node.states.push_back(state);
    }
  }
  Canonicalize(node.states);
  if (!node.bounds.has_value()) {
    node.bounds = it->second.bounds;
  }
  node.occlusion_determined |= it->second.occlusion_determined;

  // A describing adapter's default means it lacks that optional knowledge;
  // it is not evidence that another producer's established fact disappeared.
  if (!incoming_has_exact_form_semantics && !node.destination.has_value()) {
    node.destination = it->second.destination;
  }
  if (!incoming_has_exact_form_semantics &&
      node.sensitivity == Sensitivity::kUnknownSensitive) {
    node.sensitivity = it->second.sensitivity;
  }
  if (!incoming_has_exact_form_semantics &&
      node.challenge_kind == ChallengeKind::kNone) {
    node.challenge_kind = it->second.challenge_kind;
  }
  if (!incoming_has_exact_form_semantics &&
      !node.challenge_dom_node_id.has_value()) {
    node.challenge_dom_node_id = it->second.challenge_dom_node_id;
  }
  if (!incoming_has_exact_form_semantics &&
      !node.form_field_node_ids.has_value()) {
    node.form_field_node_ids = it->second.form_field_node_ids;
  }
  if (!incoming_has_exact_form_semantics &&
      node.role == SemanticRole::kUnknownContent) {
    node.role = it->second.role;
  }
  if (!incoming_has_exact_form_semantics && node.display_label.empty()) {
    node.display_label = it->second.display_label;
  }
  if (!incoming_has_exact_form_semantics && node.actions.empty()) {
    node.actions = it->second.actions;
  }

  // Authorship is different: unknown is least trusted, not missing. All
  // producers converge under a monotonic least-trusted join, so a later
  // description cannot erase an earlier lower-trust classification.
  node.content_trust = content_metadata::LeastTrusted(
      it->second.content_trust, node.content_trust);
  node.value_changed_at_revision = std::max(
      node.value_changed_at_revision, it->second.value_changed_at_revision);

  if (!PreconditionRelevantChange(it->second, node)) {
    it->second = std::move(node);
    return;
  }

  // Name the change class by what moved. Bounds changes are droppable under
  // backpressure; destination changes never are.
  const bool destination_changed =
      !SameDestination(it->second.destination, node.destination);
  const bool role_changed = it->second.role != node.role;
  const bool bounds_only =
      !destination_changed && !role_changed &&
      it->second.display_label == node.display_label &&
      it->second.actions == node.actions &&
      it->second.sensitivity == node.sensitivity &&
      it->second.content_trust == node.content_trust &&
      it->second.challenge_kind == node.challenge_kind &&
      it->second.challenge_dom_node_id == node.challenge_dom_node_id &&
      it->second.form_field_node_ids == node.form_field_node_ids &&
      it->second.states == node.states &&
      it->second.occlusion_determined == node.occlusion_determined &&
      !SameBounds(it->second.bounds, node.bounds);
  NoteChange(destination_changed ? ChangeClass::kDestinationChanged
             : role_changed      ? ChangeClass::kRoleChanged
             : bounds_only       ? ChangeClass::kBoundsChanged
                                 : ChangeClass::kAccessibleStateChanged);
  node.last_changed = current_revision_;
  it->second = std::move(node);
}

}  // namespace taffy
