// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/dom_table_relationships.h"

#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace taffy::dom_table_relationships {
namespace {

void AccumulatePayloadBytes(size_t additional, size_t* total) {
  const size_t ceiling = std::numeric_limits<size_t>::max();
  *total = additional > ceiling - *total ? ceiling : *total + additional;
}

}  // namespace

void Append(const std::vector<TableCell>& table_cells,
            ExtractionContext& context,
            AdapterResult* result) {
  // Index once rather than comparing every header with every cell. The
  // amount of relationship work is capped by the same total-byte budget that
  // owns the result, not by an unrelated table-specific number.
  using TableAxis = std::pair<int64_t, uint32_t>;
  std::map<TableAxis, std::vector<const TableCell*>> cells_by_column;
  std::map<TableAxis, std::vector<const TableCell*>> cells_by_row;
  bool stopped = false;
  size_t pending_payload_bytes = 0;

  for (const SemanticNode& node : result->nodes) {
    if (!context.ledger->CheckDeadline()) {
      stopped = true;
      break;
    }
    AccumulatePayloadBytes(BudgetLedger::ConservativeNodeBytes(node),
                           &pending_payload_bytes);
  }
  for (const SemanticEdge& edge : result->edges) {
    if (stopped || !context.ledger->CheckDeadline()) {
      stopped = true;
      break;
    }
    AccumulatePayloadBytes(BudgetLedger::ConservativeEdgeBytes(edge),
                           &pending_payload_bytes);
  }
  if (!stopped && !context.ledger->CheckBytesAvailable(pending_payload_bytes)) {
    stopped = true;
  }

  for (const TableCell& cell : table_cells) {
    if (stopped || !context.ledger->CheckDeadline()) {
      stopped = true;
      break;
    }
    if (!cell.is_header) {
      cells_by_column[{cell.table_key, cell.column_index}].push_back(&cell);
      cells_by_row[{cell.table_key, cell.row_index}].push_back(&cell);
    }
  }

  for (const TableCell& header : table_cells) {
    if (stopped || !context.ledger->CheckDeadline()) {
      break;
    }
    if (!header.is_header) {
      continue;
    }
    const bool column_header = header.scope == "col" ||
                               header.scope == "colgroup" ||
                               (header.scope.empty() && header.row_index == 0);
    const bool row_header = header.scope == "row" ||
                            header.scope == "rowgroup" ||
                            (header.scope.empty() && header.column_index == 0 &&
                             header.row_index != 0);
    if (!column_header && !row_header) {
      continue;
    }

    const TableAxis axis = {header.table_key, column_header
                                                  ? header.column_index
                                                  : header.row_index};
    const auto& index = column_header ? cells_by_column : cells_by_row;
    const auto cells = index.find(axis);
    if (cells == index.end()) {
      continue;
    }
    for (const TableCell* cell : cells->second) {
      if (!context.ledger->CheckDeadline()) {
        stopped = true;
        break;
      }
      SemanticEdge edge;
      edge.from_frame_id = context.store->frame_id();
      edge.from_node_id = header.node_id;
      edge.to_frame_id = context.store->frame_id();
      edge.to_node_id = cell->node_id;
      edge.relationship =
          column_header ? EdgeType::kColumnHeaderFor : EdgeType::kRowHeaderFor;
      edge.inferred = true;
      AccumulatePayloadBytes(BudgetLedger::ConservativeEdgeBytes(edge),
                             &pending_payload_bytes);
      if (!context.ledger->CheckBytesAvailable(pending_payload_bytes)) {
        stopped = true;
        break;
      }
      result->edges.push_back(std::move(edge));
    }
  }
}

}  // namespace taffy::dom_table_relationships
