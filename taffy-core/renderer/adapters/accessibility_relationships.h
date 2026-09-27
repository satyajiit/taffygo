// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_ACCESSIBILITY_RELATIONSHIPS_H_
#define TAFFY_RENDERER_ADAPTERS_ACCESSIBILITY_RELATIONSHIPS_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "taffy/renderer/adapters/adapter.h"

namespace ui {
struct AXNodeData;
}  // namespace ui

namespace taffy {

// Holds authored AX relationships until every target has an identity, then
// appends only edges whose target the producing walk actually described.
// Collection and resolution share one deadline and one resident-memory cap.
class AccessibilityRelationships {
 public:
  explicit AccessibilityRelationships(const ExtractionContext& context);
  AccessibilityRelationships(const AccessibilityRelationships&) = delete;
  AccessibilityRelationships& operator=(const AccessibilityRelationships&) =
      delete;
  ~AccessibilityRelationships();

  void Collect(const ui::AXNodeData& data,
               const SemanticNodeId& from,
               BudgetLedger& ledger);
  void Resolve(ExtractionContext& context, AdapterResult& result);

  bool incomplete() const {
    return count_limit_reached_ || target_unresolved_ || deadline_reached_;
  }

 private:
  struct PendingRelation {
    SemanticNodeId from;
    int32_t to_ax_id;
    EdgeType relationship;
  };

  void Append(const std::vector<int32_t>& targets,
              const SemanticNodeId& from,
              EdgeType relationship,
              BudgetLedger& ledger);

  const size_t max_pending_relations_;
  std::vector<PendingRelation> pending_;
  bool count_limit_reached_ = false;
  bool target_unresolved_ = false;
  bool deadline_reached_ = false;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_ACCESSIBILITY_RELATIONSHIPS_H_
