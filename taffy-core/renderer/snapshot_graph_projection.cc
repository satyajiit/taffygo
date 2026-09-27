// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/snapshot_graph_projection.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "taffy/renderer/content_metadata.h"
#include "taffy/renderer/wire_conversions.h"

namespace taffy {
namespace {

bool InScope(ExtractionScope scope, const SemanticNode& node) {
  if (node.role == SemanticRole::kDocument &&
      scope != ExtractionScope::kSelection &&
      scope != ExtractionScope::kSection) {
    return true;
  }
  switch (scope) {
    case ExtractionScope::kDocument:
      return true;
    case ExtractionScope::kInteractive:
      return std::ranges::any_of(node.actions, [](ActionKind action) {
        return action != ActionKind::kScrollIntoView;
      });
    case ExtractionScope::kViewport:
      return std::ranges::find(node.states, NodeState::kOffscreen) ==
                 node.states.end() &&
             std::ranges::find(node.states, NodeState::kNotVisible) ==
                 node.states.end();
    case ExtractionScope::kSelection:
      // Selection membership must come from SelectionAdapter, not this
      // generic state. Accessibility also marks selected controls with the
      // same state even when they are outside the live document selection.
      return false;
    case ExtractionScope::kSection:
      // Membership is computed from the exact root and CONTAINS edges below.
      return false;
  }
}

std::set<SemanticNodeId> SectionMembership(const SemanticNodeId& root,
                                           const ExtractedGraph& graph,
                                           BudgetLedger* ledger,
                                           bool* incomplete) {
  std::map<SemanticNodeId, std::vector<SemanticNodeId>> children;
  std::map<SemanticNodeId, std::vector<SemanticNodeId>> equivalents;
  for (const SemanticEdge& edge : graph.edges) {
    if (!ledger->CheckDeadline()) {
      *incomplete = true;
      return {};
    }
    if (edge.relationship == EdgeType::kContains) {
      children[edge.from_node_id].push_back(edge.to_node_id);
    } else if (edge.relationship == EdgeType::kSameEntityAs) {
      // Identity is symmetric even though the wire row has a direction.
      equivalents[edge.from_node_id].push_back(edge.to_node_id);
      equivalents[edge.to_node_id].push_back(edge.from_node_id);
    }
  }

  // CONTAINS alone is the membership authority. In particular, an AX group
  // or a DOM fieldset that happens to contain a control cannot become a form
  // root, and the generic subtree below an AX equivalent cannot broaden the
  // section after the exact root has been established.
  std::set<SemanticNodeId> kept;
  std::vector<SemanticNodeId> pending = {root};
  while (!pending.empty()) {
    if (!ledger->CheckDeadline()) {
      *incomplete = true;
      break;
    }
    SemanticNodeId current = std::move(pending.back());
    pending.pop_back();
    if (!kept.insert(current).second) {
      continue;
    }
    auto it = children.find(current);
    if (it == children.end()) {
      continue;
    }
    pending.insert(pending.end(), it->second.begin(), it->second.end());
  }

  // A form control's safe accessible label can live on its exact AX identity
  // while its actions live on the exact DOM identity. Retain only that
  // SAME_ENTITY_AS closure after membership is fixed; never resume the
  // CONTAINS walk from identities added here.
  pending.assign(kept.begin(), kept.end());
  while (!pending.empty()) {
    if (!ledger->CheckDeadline()) {
      *incomplete = true;
      break;
    }
    SemanticNodeId current = std::move(pending.back());
    pending.pop_back();
    auto it = equivalents.find(current);
    if (it == equivalents.end()) {
      continue;
    }
    for (const SemanticNodeId& equivalent : it->second) {
      if (kept.insert(equivalent).second) {
        pending.push_back(equivalent);
      }
    }
  }
  return kept;
}

size_t AddPayloadBytes(size_t left, size_t right) {
  const size_t ceiling = std::numeric_limits<size_t>::max();
  return right > ceiling - left ? ceiling : left + right;
}

size_t ConservativeWireWarningBytes(const mojom::SnapshotWarning& warning) {
  size_t bytes = sizeof(mojom::SnapshotWarning);
  if (warning.frame_id.has_value()) {
    bytes = AddPayloadBytes(bytes, warning.frame_id->size());
  }
  if (warning.node_id.has_value()) {
    bytes = AddPayloadBytes(bytes, warning.node_id->size());
  }
  if (warning.detail_code.has_value()) {
    bytes = AddPayloadBytes(bytes, warning.detail_code->size());
  }
  return bytes;
}

bool AppendWarning(BudgetLedger* ledger,
                   mojom::SnapshotWarningPtr warning,
                   std::vector<mojom::SnapshotWarningPtr>* warnings) {
  if (!ledger->ChargeBytes(ConservativeWireWarningBytes(*warning))) {
    return false;
  }
  warnings->push_back(std::move(warning));
  return true;
}

}  // namespace

SnapshotProjectionSummary ProjectSnapshotGraph(
    ExtractionScope scope,
    const std::optional<SemanticNodeId>& section_root,
    const std::optional<SemanticNodeId>& media_root,
    const std::set<SemanticNodeId>& selection_node_ids,
    const ExtractedGraph& graph,
    bool dom_stopped_at_shadow_boundary,
    BudgetLedger* ledger,
    mojom::PageSnapshot* snapshot,
    std::vector<mojom::SnapshotWarningPtr>* warnings) {
  SnapshotProjectionSummary summary;

  if (dom_stopped_at_shadow_boundary) {
    bool composed_tree_content = false;
    bool pass_complete = true;
    for (const SemanticNode& node : graph.nodes) {
      if (!ledger->CheckDeadline()) {
        pass_complete = false;
        summary.incomplete = true;
        break;
      }
      if (node.projection_path ==
          ProjectionPath::kComposedTreeThroughAccessibility) {
        composed_tree_content = true;
        break;
      }
    }
    if (pass_complete) {
      auto warning = mojom::SnapshotWarning::New();
      warning->code = mojom::WarningCode::kClosedShadowRootNotProjected;
      warning->detail_code =
          composed_tree_content ? "shadow-content-reached-through-accessibility"
                                : "shadow-content-not-reached";
      summary.incomplete |=
          !AppendWarning(ledger, std::move(warning), warnings);
      summary.incomplete |= !composed_tree_content;
    }
  }

  std::set<SemanticNodeId> kept;
  if (scope == ExtractionScope::kSection && section_root.has_value()) {
    kept = SectionMembership(*section_root, graph, ledger, &summary.incomplete);
  } else if (scope == ExtractionScope::kDocument && media_root.has_value()) {
    // A media observation names one exact node. Do not include its DOM
    // ancestors, siblings, fallback text, or children merely because the
    // contract still calls the outer scope DOCUMENT.
    kept.insert(*media_root);
  }
  for (size_t i = 0; i < graph.nodes.size(); ++i) {
    if (!ledger->CheckDeadline()) {
      ledger->NoteOmittedNodes(graph.nodes.size() - i,
                               /*could_change_answer=*/true);
      summary.incomplete = true;
      break;
    }
    const SemanticNode& node = graph.nodes[i];
    const bool in_requested_scope =
        media_root.has_value()               ? kept.contains(node.node_id)
        : scope == ExtractionScope::kSection ? kept.contains(node.node_id)
        : scope == ExtractionScope::kSelection
            ? selection_node_ids.contains(node.node_id)
            : InScope(scope, node);
    if (in_requested_scope) {
      kept.insert(node.node_id);
    } else {
      ++summary.dropped_by_scope;
    }
  }

  std::optional<RendererContentTrust> lowest_content_trust;
  bool content_signal_detected = false;
  std::set<SemanticNodeId> serialized;
  for (const SemanticNode& node : graph.nodes) {
    if (!kept.contains(node.node_id)) {
      continue;
    }
    if (!ledger->CheckDeadline()) {
      ledger->NoteOmittedNodes(kept.size() - serialized.size(),
                               /*could_change_answer=*/true);
      summary.incomplete = true;
      break;
    }
    if (node.value_descriptor.has_value() &&
        node.value_descriptor->kind == ValueKind::kSecretWithheld) {
      ++summary.suppressed_secret_values;
      ++summary.sensitive_zones;
    }
    if (node.sensitivity != Sensitivity::kNotSensitive) {
      ++summary.redacted_fields;
    }
    lowest_content_trust = lowest_content_trust.has_value()
                               ? content_metadata::LeastTrusted(
                                     *lowest_content_trust, node.content_trust)
                               : node.content_trust;
    content_signal_detected |= node.content_signals != 0;
    for (const TextRun& run : node.text_runs) {
      lowest_content_trust = content_metadata::LeastTrusted(
          *lowest_content_trust, run.content_trust);
      content_signal_detected |= run.content_signals != 0;
    }
    snapshot->nodes.push_back(wire::ToMojom(node));
    serialized.insert(node.node_id);
  }
  snapshot->lowest_content_trust = wire::ToMojom(
      lowest_content_trust.value_or(RendererContentTrust::kUnknownUntrusted));

  if (content_signal_detected) {
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kInjectionSignalDetected;
    warning->detail_code = "content-signal-detected";
    summary.incomplete |= !AppendWarning(ledger, std::move(warning), warnings);
  }

  for (const SemanticEdge& edge : graph.edges) {
    if (!ledger->CheckDeadline()) {
      summary.incomplete = true;
      break;
    }
    if (serialized.contains(edge.from_node_id) &&
        serialized.contains(edge.to_node_id)) {
      snapshot->edges.push_back(wire::ToMojom(edge));
    }
  }
  summary.incomplete |= !ledger->CheckDeadline();
  return summary;
}

}  // namespace taffy
