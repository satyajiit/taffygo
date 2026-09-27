// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/semantic_graph_store.h"

#include <algorithm>
#include <utility>

namespace taffy {

bool SemanticGraphStore::AnnotateLiveNode(
    SemanticNodeId id,
    const std::vector<NodeState>& states,
    const std::optional<NodeBounds>& bounds,
    bool occlusion_determined) {
  if (IsRetired(id)) {
    return false;
  }
  auto it = live_by_id_.find(id);
  if (it == live_by_id_.end()) {
    return false;
  }

  LiveNode updated = it->second;
  for (NodeState state : states) {
    // Appended, not replaced. Two adapters that disagree about visibility
    // both get to say so, and the precondition check reads a required state
    // as "asserted" and a forbidden state as "asserted" too - so the strict
    // reading wins without this function having to pick a winner.
    if (std::ranges::find(updated.states, state) == updated.states.end()) {
      updated.states.push_back(state);
    }
  }
  if (bounds.has_value()) {
    updated.bounds = bounds;
  }
  // One-way: an adapter that probed cannot be un-probed by one that did not.
  updated.occlusion_determined |= occlusion_determined;

  UpsertLiveNode(std::move(updated));
  return true;
}

const SemanticGraphStore::LiveNode* SemanticGraphStore::FindLive(
    SemanticNodeId id) const {
  if (IsRetired(id)) {
    return nullptr;
  }
  auto it = live_by_id_.find(id);
  return it == live_by_id_.end() ? nullptr : &it->second;
}

bool SemanticGraphStore::IsChallengePresentationTarget(
    int64_t dom_node_id,
    ChallengeKind kind) const {
  if (dom_node_id == 0 || kind == ChallengeKind::kNone) {
    return false;
  }
  return std::ranges::any_of(
      live_by_id_, [dom_node_id, kind](const auto& entry) {
        const LiveNode& node = entry.second;
        return node.form_semantics_authoritative &&
               node.challenge_kind == kind &&
               node.challenge_dom_node_id == dom_node_id;
      });
}

std::optional<std::vector<int64_t>>
SemanticGraphStore::VisualRedactionDomNodeIds(size_t maximum_targets) const {
  std::vector<int64_t> dom_node_ids;
  for (const auto& [node_id, node] : live_by_id_) {
    static_cast<void>(node_id);
    if (node.form_semantics_authoritative &&
        node.challenge_kind != ChallengeKind::kNone) {
      return std::nullopt;
    }
    const bool prohibited =
        node.sensitivity == Sensitivity::kCredential ||
        node.sensitivity == Sensitivity::kOneTimeCode ||
        node.sensitivity == Sensitivity::kChallengeResponse;
    if (!prohibited) {
      continue;
    }
    if (node.dom_key.space != IdentitySpace::kDom ||
        node.dom_key.dom_node_id == 0) {
      return std::nullopt;
    }
    const bool duplicate = std::ranges::contains(dom_node_ids,
                                                 node.dom_key.dom_node_id);
    if (!duplicate) {
      dom_node_ids.push_back(node.dom_key.dom_node_id);
      if (dom_node_ids.size() > maximum_targets) {
        return std::nullopt;
      }
    }
  }
  return dom_node_ids;
}

}  // namespace taffy
