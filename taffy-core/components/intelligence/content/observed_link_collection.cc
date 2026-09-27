// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/observed_link_collection.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/logging.h"
#include "taffy/components/intelligence/content/bip_graph_payload.h"
#include "url/gurl.h"

namespace taffy {
namespace {

bool DestinationWasRequested(const ObservationRequest& request) {
  for (SemanticField field : request.requested_fields) {
    if (field == SemanticField::kDestination) {
      return true;
    }
  }
  return false;
}

bool SupportsActivate(const mojom::SemanticNode& node) {
  if (node.actions.size() > kMaxBipNodeActions) {
    return false;
  }
  for (mojom::ActionType action : node.actions) {
    if (action == mojom::ActionType::kActivate) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool CollectTransientObservedLinks(
    const ObservationRequest& request,
    const mojom::PageSnapshot& snapshot,
    std::vector<TransientObservedLink>* observed_links) {
  CHECK(observed_links);
  observed_links->clear();
  if (!DestinationWasRequested(request)) {
    return true;
  }

  std::map<std::string, TransientObservedLink> unique;
  std::set<std::string> ambiguous;
  // Why a node the model can see as a link is not one it may open. Six
  // filters stand between a snapshot node and this table, and a node that
  // fails any of them reaches the model rendered as `link` and is then
  // refused `kNodeGone` with nothing said. Every counter here is a count of
  // nodes; none of them is a fact about the page (decision 0177).
  size_t link_role = 0u;
  size_t no_activate = 0u;
  size_t no_destination = 0u;
  size_t not_full_url = 0u;
  size_t unusable_url = 0u;
  for (const mojom::SemanticNodePtr& node : snapshot.nodes) {
    if (!node || node->role != mojom::SemanticRole::kLink ||
        node->node_id.empty() || node->node_id.size() > kMaxBipIdentityBytes) {
      continue;
    }
    ++link_role;
    if (!SupportsActivate(*node)) {
      ++no_activate;
      continue;
    }
    if (!node->destination || !node->destination->url_metadata) {
      ++no_destination;
      continue;
    }
    const mojom::UrlMetadata& metadata = *node->destination->url_metadata;
    if (metadata.disclosure != mojom::UrlDisclosure::kFullUrl ||
        !metadata.url || metadata.url->empty() ||
        metadata.url->size() > kMaxTransientObservedLinkDestinationBytes) {
      ++not_full_url;
      continue;
    }
    const GURL parsed(*metadata.url);
    if (!parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS() ||
        parsed.has_username() || parsed.has_password() ||
        parsed.spec() != *metadata.url) {
      ++unusable_url;
      continue;
    }
    if (!unique
             .emplace(node->node_id,
                      TransientObservedLink{
                          .node_id = SemanticNodeId{node->node_id},
                          .normalized_destination = parsed.spec(),
                          .opens_new_tab = node->destination->opens_new_tab,
                          .is_download = node->destination->is_download,
                      })
             .second) {
      // Two link nodes wearing one identifier. A capability minted for that
      // identifier would name whichever of them the table happened to keep, so
      // neither may be resolvable — but that is a statement about those two
      // links and not about the page. Dropping the identifier from the table
      // makes it unresolvable exactly as failing closed did; taking the whole
      // observation down with it, which is what this used to do, cost the task
      // every node on the page as well (decision 0172).
      ambiguous.insert(node->node_id);
    }
  }
  for (const std::string& node_id : ambiguous) {
    unique.erase(node_id);
  }
  if (!ambiguous.empty()) {
    LOG(WARNING) << "[taffy_observed_link_ambiguous] ids=" << ambiguous.size()
                 << " links=" << unique.size();
  }
  LOG(WARNING) << "[taffy_observed_link_collect] nodes=" << snapshot.nodes.size()
               << " link_role=" << link_role << " kept=" << unique.size()
               << " no_activate=" << no_activate
               << " no_destination=" << no_destination
               << " not_full_url=" << not_full_url
               << " unusable_url=" << unusable_url;
  observed_links->reserve(unique.size());
  for (auto& [node_id, link] : unique) {
    observed_links->push_back(std::move(link));
  }
  return true;
}

}  // namespace taffy
