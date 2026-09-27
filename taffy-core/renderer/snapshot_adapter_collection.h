// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_SNAPSHOT_ADAPTER_COLLECTION_H_
#define TAFFY_RENDERER_SNAPSHOT_ADAPTER_COLLECTION_H_

#include <set>
#include <vector>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/page_capabilities.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

class SemanticGraphStore;

// The adapter-run half of snapshot construction. It owns no authority and no
// envelope identity; it runs only the named adapters against one shared budget
// and returns their graph, reports, warnings, and fail-closed result.
struct SnapshotAdapterCollection {
  PageCapabilities capabilities;
  ExtractedGraph graph;
  // Exact membership authored by SelectionAdapter. NodeState::kSelected is
  // also used by accessibility for selected controls, so it is not sufficient
  // authority for a selection-scoped projection.
  std::set<SemanticNodeId> selection_node_ids;
  std::vector<mojom::AdapterReportPtr> reports;
  std::vector<mojom::SnapshotWarningPtr> warnings;
  bool incomplete = false;
  bool conflicted = false;
  bool dom_stopped_at_shadow_boundary = false;
  bool refused = false;
  mojom::ObservationResultCode refusal_code =
      mojom::ObservationResultCode::kInternalError;
};

SnapshotAdapterCollection CollectSnapshotAdapters(
    blink::WebLocalFrame* frame,
    SemanticGraphStore* store,
    const BrowserSuppliedFacts& browser_facts,
    const mojom::SnapshotRequest& request,
    ExtractionScope scope,
    const ObservationLimits& limits,
    BudgetLedger* ledger,
    PageCapabilities capabilities);

}  // namespace taffy

#endif  // TAFFY_RENDERER_SNAPSHOT_ADAPTER_COLLECTION_H_
