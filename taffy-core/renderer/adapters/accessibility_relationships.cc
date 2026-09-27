// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/accessibility_relationships.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "ui/accessibility/ax_enums.mojom-shared.h"
#include "ui/accessibility/ax_node_data.h"

namespace taffy {

AccessibilityRelationships::AccessibilityRelationships(
    const ExtractionContext& context)
    : max_pending_relations_(
          std::max<size_t>(1u,
                           context.limits->snapshot().max_total_bytes() /
                               sizeof(PendingRelation))) {}

AccessibilityRelationships::~AccessibilityRelationships() = default;

void AccessibilityRelationships::Collect(const ui::AXNodeData& data,
                                         const SemanticNodeId& from,
                                         BudgetLedger& ledger) {
  Append(data.GetIntListAttribute(ax::mojom::IntListAttribute::kLabelledbyIds),
         from, EdgeType::kLabels, ledger);
  Append(data.GetIntListAttribute(ax::mojom::IntListAttribute::kDescribedbyIds),
         from, EdgeType::kDescribes, ledger);
  Append(data.GetIntListAttribute(ax::mojom::IntListAttribute::kControlsIds),
         from, EdgeType::kControls, ledger);
}

void AccessibilityRelationships::Resolve(ExtractionContext& context,
                                         AdapterResult& result) {
  for (const PendingRelation& relation : pending_) {
    if (!context.ledger->CheckDeadline()) {
      deadline_reached_ = true;
      break;
    }
    const std::optional<SemanticNodeId> target =
        context.store->Lookup(context.store->MakeKey(
            SemanticGraphStore::IdentitySpace::kAccessibility,
            relation.to_ax_id));
    if (!target.has_value()) {
      target_unresolved_ = true;
      continue;
    }
    SemanticEdge edge;
    edge.from_frame_id = context.store->frame_id();
    edge.from_node_id = relation.from;
    edge.to_frame_id = context.store->frame_id();
    edge.to_node_id = target.value();
    edge.relationship = relation.relationship;
    edge.inferred = false;
    result.edges.push_back(std::move(edge));
  }

  if (target_unresolved_) {
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "authored-relation-target-not-described");
  }
  if (count_limit_reached_) {
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "authored-relation-count-bound");
  }
}

void AccessibilityRelationships::Append(const std::vector<int32_t>& targets,
                                        const SemanticNodeId& from,
                                        EdgeType relationship,
                                        BudgetLedger& ledger) {
  if (count_limit_reached_ || deadline_reached_) {
    return;
  }
  for (int32_t target : targets) {
    if (!ledger.CheckDeadline()) {
      deadline_reached_ = true;
      return;
    }
    if (pending_.size() >= max_pending_relations_) {
      count_limit_reached_ = true;
      return;
    }
    pending_.push_back({from, target, relationship});
  }
}

}  // namespace taffy
