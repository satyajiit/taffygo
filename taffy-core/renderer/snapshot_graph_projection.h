// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_SNAPSHOT_GRAPH_PROJECTION_H_
#define TAFFY_RENDERER_SNAPSHOT_GRAPH_PROJECTION_H_

#include <stdint.h>

#include <optional>
#include <set>
#include <vector>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// The counters produced while narrowing an extracted graph to the requested
// scope and materializing its wire projection.
struct SnapshotProjectionSummary {
  bool incomplete = false;
  uint32_t dropped_by_scope = 0;
  uint32_t suppressed_secret_values = 0;
  uint32_t sensitive_zones = 0;
  uint32_t redacted_fields = 0;
};

SnapshotProjectionSummary ProjectSnapshotGraph(
    ExtractionScope scope,
    const std::optional<SemanticNodeId>& section_root,
    const std::optional<SemanticNodeId>& media_root,
    const std::set<SemanticNodeId>& selection_node_ids,
    const ExtractedGraph& graph,
    bool dom_stopped_at_shadow_boundary,
    BudgetLedger* ledger,
    mojom::PageSnapshot* snapshot,
    std::vector<mojom::SnapshotWarningPtr>* warnings);

}  // namespace taffy

#endif  // TAFFY_RENDERER_SNAPSHOT_GRAPH_PROJECTION_H_
