// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/snapshot_adapter_collection.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "taffy/renderer/adapters/adapter_registry.h"
#include "taffy/renderer/content_metadata.h"
#include "taffy/renderer/field_redaction.h"
#include "taffy/renderer/snapshot_capability_signals.h"
#include "taffy/renderer/wire_conversions.h"

namespace taffy {
namespace {

template <typename T>
void AppendUnique(std::vector<T>* destination, std::vector<T> source) {
  for (T& value : source) {
    if (std::ranges::find(*destination, value) == destination->end()) {
      destination->push_back(std::move(value));
    }
  }
}

// Producing adapters intentionally converge on one semantic identifier when
// they describe the same DOM node. The graph framing, and the isolated core
// that validates it, require identifiers to be unique. Keep the first
// (highest-precedence) scalar answer, fill facts it did not know, and retain
// every source/evidence row plus the union of set-shaped facts. This preserves
// disagreement without emitting two contradictory rows under one identity.
void MergeNode(SemanticNode incoming, SemanticNode* existing) {
  if (!existing->name.has_value()) {
    existing->name = std::move(incoming.name);
  }
  if (!existing->description.has_value()) {
    existing->description = std::move(incoming.description);
  }
  existing->text_runs.insert(
      existing->text_runs.end(),
      std::make_move_iterator(incoming.text_runs.begin()),
      std::make_move_iterator(incoming.text_runs.end()));
  AppendUnique(&existing->states, std::move(incoming.states));
  if (!existing->value_descriptor.has_value()) {
    existing->value_descriptor = std::move(incoming.value_descriptor);
  }
  if (!existing->destination.has_value()) {
    existing->destination = std::move(incoming.destination);
  }
  if (!existing->bounds.has_value()) {
    existing->bounds = incoming.bounds;
  }
  AppendUnique(&existing->actions, std::move(incoming.actions));
  AppendUnique(&existing->sources, std::move(incoming.sources));
  for (Attribute& attribute : incoming.attributes) {
    const bool already_present =
        std::ranges::any_of(existing->attributes, [&](const Attribute& held) {
          return held.key == attribute.key && held.value == attribute.value;
        });
    if (!already_present) {
      existing->attributes.push_back(std::move(attribute));
    }
  }
  existing->evidence.insert(existing->evidence.end(),
                            std::make_move_iterator(incoming.evidence.begin()),
                            std::make_move_iterator(incoming.evidence.end()));
  existing->confidence = std::max(existing->confidence, incoming.confidence);
  // Precedence chooses authored facts such as role and name. Sensitivity is
  // deliberately different: disagreement resolves upward, always. Keeping a
  // higher-precedence but milder classification would turn adapter merging
  // into a path for page content to lower the browser's write gate.
  existing->sensitivity = SensitivityClassifier::Stricter(existing->sensitivity,
                                                          incoming.sensitivity);
  existing->content_trust = content_metadata::LeastTrusted(
      existing->content_trust, incoming.content_trust);
  existing->content_signals |= incoming.content_signals;

  // The wire comments promise these two lists are canonical. Do it on the
  // graph that actually crosses, not only on the separate live-node store.
  std::ranges::sort(existing->actions);
  existing->actions.erase(std::ranges::unique(existing->actions).begin(),
                          existing->actions.end());
  std::ranges::sort(existing->states);
  existing->states.erase(std::ranges::unique(existing->states).begin(),
                         existing->states.end());
}

using EdgeKey = std::
    tuple<std::string, std::string, std::string, std::string, EdgeType, bool>;

EdgeKey KeyFor(const SemanticEdge& edge) {
  return {edge.from_frame_id.value(), edge.from_node_id.value(),
          edge.to_frame_id.value(),   edge.to_node_id.value(),
          edge.relationship,          edge.inferred};
}

void ApplyAnnotation(const NodeAnnotation& annotation, SemanticNode* node) {
  for (NodeState state : annotation.states) {
    if (std::ranges::find(node->states, state) == node->states.end()) {
      node->states.push_back(state);
    }
  }
  if (annotation.bounds.has_value()) {
    node->bounds = annotation.bounds;
  }
  node->evidence.insert(node->evidence.end(), annotation.evidence.begin(),
                        annotation.evidence.end());
}

size_t ConservativeAdapterReportBytes(const mojom::AdapterReport& report) {
  size_t bytes = sizeof(mojom::AdapterReport);
  if (report.detail_code.has_value()) {
    const size_t remaining = std::numeric_limits<size_t>::max() - bytes;
    bytes += std::min(remaining, report.detail_code->size());
  }
  return bytes;
}

bool AppendReport(BudgetLedger* ledger,
                  mojom::AdapterReportPtr report,
                  std::vector<mojom::AdapterReportPtr>* reports) {
  if (!ledger->ChargeBytes(ConservativeAdapterReportBytes(*report))) {
    return false;
  }
  reports->push_back(std::move(report));
  return true;
}

struct AdapterRequestMatch {
  bool requested = false;
  bool required = false;
};

AdapterRequestMatch MatchAdapterRequest(const mojom::SnapshotRequest& request,
                                        AdapterKind kind) {
  AdapterRequestMatch outcome;
  for (const mojom::AdapterRequirementPtr& requirement : request.adapters) {
    if (wire::FromMojom(requirement->adapter) != kind) {
      continue;
    }
    outcome.requested = true;
    outcome.required |= requirement->requirement ==
                        mojom::AdapterRequirementLevel::kRequired;
  }
  return outcome;
}

bool ScopeRequiresAdapter(ExtractionScope scope, AdapterKind kind) {
  return (scope == ExtractionScope::kSelection &&
          kind == AdapterKind::kSelection) ||
         (scope == ExtractionScope::kViewport && kind == AdapterKind::kLayout);
}

bool HasSelectedState(const std::vector<NodeState>& states) {
  return std::ranges::find(states, NodeState::kSelected) != states.end();
}

}  // namespace

SnapshotAdapterCollection CollectSnapshotAdapters(
    blink::WebLocalFrame* frame,
    SemanticGraphStore* store,
    const BrowserSuppliedFacts& browser_facts,
    const mojom::SnapshotRequest& request,
    ExtractionScope scope,
    const ObservationLimits& limits,
    BudgetLedger* ledger,
    PageCapabilities capabilities) {
  SnapshotAdapterCollection result;
  result.capabilities = std::move(capabilities);
  std::map<SemanticNodeId, size_t> node_index;
  std::set<EdgeKey> edge_keys;

  for (AdapterRegistry::Entry& entry : AdapterRegistry::CreateAll()) {
    const AdapterRequestMatch request_match =
        MatchAdapterRequest(request, entry.kind);
    const bool required_by_scope = ScopeRequiresAdapter(scope, entry.kind);
    if (!request_match.requested && !required_by_scope) {
      continue;
    }
    if (!ledger->CheckDeadline()) {
      result.incomplete = true;
      break;
    }

    if (result.capabilities.For(entry.kind).availability ==
        AdapterAvailability::kUnsupported) {
      result.capabilities.RecordRunOutcome(entry.kind,
                                           AdapterStatus::kUnsupported);
      if (!AppendReport(ledger,
                        snapshot_capability_signals::MakeReport(
                            result.capabilities.For(entry.kind),
                            AdapterStatus::kUnsupported),
                        &result.reports)) {
        result.incomplete = true;
        break;
      }
      if (request_match.required || required_by_scope) {
        result.refused = true;
        result.refusal_code = mojom::ObservationResultCode::kUnsupported;
        auto warning = mojom::SnapshotWarning::New();
        warning->code = mojom::WarningCode::kAdapterUnavailable;
        warning->detail_code = "required-adapter-unsupported";
        result.warnings.push_back(std::move(warning));
        return result;
      }
      // OPTIONAL means this evidence is welcome, not necessary for a usable
      // result. Its report still names the absence, while the required
      // evidence produced by the other adapters remains complete.
      continue;
    }

    ExtractionContext context(frame, store, ledger, limits, &result.graph,
                              browser_facts, scope);
    AdapterResult adapter_result = entry.adapter->Run(context);
    result.capabilities.RecordRunOutcome(entry.kind, adapter_result.status);
    if (!AppendReport(
            ledger,
            snapshot_capability_signals::MakeReport(
                result.capabilities.For(entry.kind), adapter_result.status),
            &result.reports)) {
      result.incomplete = true;
      break;
    }

    bool accept_payload = true;
    for (const ObservationWarning& warning : adapter_result.warnings) {
      if (!ledger->CheckDeadline() || !ledger->ChargeWarning(warning)) {
        result.incomplete = true;
        accept_payload = false;
        break;
      }
      result.dom_stopped_at_shadow_boundary |=
          warning.detail_code == "dom-stops-at-shadow-boundary";
      result.warnings.push_back(wire::ToMojom(warning));
    }

    switch (adapter_result.status) {
      case AdapterStatus::kIncomplete:
        result.incomplete = true;
        break;
      case AdapterStatus::kUnsupported:
      case AdapterStatus::kFailed:
        break;
      case AdapterStatus::kConflicted:
        // Two adapters report this status and they mean opposite things.
        //
        // Every content adapter means "the sources disagree about this page",
        // which protocol section 7.5 asks to be preserved rather than
        // resolved: the graph carries competing candidates and the reading is
        // a reading. `kDocumentMetadata` means something else entirely. It is
        // the one adapter that compares a browser-stated fact with what this
        // process believes, and its only conflict is
        // `origin-outside-browser-allowed-set` — this renderer saying it is
        // not on the document the browser named. There is no reading of the
        // requested document here to caveat, so it refuses outright instead
        // of travelling as a graph the browser would then have to decide
        // about (decision 0207).
        // Not `kConflicted`: that code now means a reading that happened, and
        // the whole point of this branch is that no reading of the requested
        // document did.
        if (entry.kind == AdapterKind::kDocumentMetadata) {
          result.refused = true;
          result.refusal_code = mojom::ObservationResultCode::kInternalError;
          auto warning = mojom::SnapshotWarning::New();
          warning->code = mojom::WarningCode::kAdapterFailed;
          warning->detail_code = "document-identity-disagrees-with-browser";
          result.warnings.push_back(std::move(warning));
          return result;
        }
        result.conflicted = true;
        break;
      case AdapterStatus::kOk:
        break;
    }
    if ((request_match.required || required_by_scope) &&
        (adapter_result.status == AdapterStatus::kUnsupported ||
         adapter_result.status == AdapterStatus::kFailed)) {
      result.refused = true;
      result.refusal_code = mojom::ObservationResultCode::kUnsupported;
      auto warning = mojom::SnapshotWarning::New();
      warning->code = mojom::WarningCode::kAdapterUnavailable;
      warning->detail_code = "required-adapter-failed-during-collection";
      result.warnings.push_back(std::move(warning));
      return result;
    }
    if (adapter_result.status == AdapterStatus::kUnsupported ||
        adapter_result.status == AdapterStatus::kFailed) {
      // A failed optional adapter contributes no graph payload. Its bounded
      // warning and report are enough to preserve the failure honestly, and
      // collection continues so one optional source cannot starve the later
      // requested adapters.
      continue;
    }

    if (accept_payload) {
      for (size_t i = 0; i < adapter_result.nodes.size(); ++i) {
        SemanticNode& node = adapter_result.nodes[i];
        if (!ledger->CheckDeadline() || !ledger->ChargeNodePayload(node)) {
          ledger->NoteOmittedNodes(adapter_result.nodes.size() - i,
                                   /*could_change_answer=*/true);
          result.incomplete = true;
          accept_payload = false;
          break;
        }
        if (entry.kind == AdapterKind::kSelection &&
            HasSelectedState(node.states)) {
          result.selection_node_ids.insert(node.node_id);
        }
        const auto existing = node_index.find(node.node_id);
        if (existing == node_index.end()) {
          node_index.emplace(node.node_id, result.graph.nodes.size());
          result.graph.nodes.push_back(std::move(node));
        } else {
          MergeNode(std::move(node), &result.graph.nodes[existing->second]);
        }
      }
    }
    if (accept_payload) {
      for (SemanticEdge& edge : adapter_result.edges) {
        if (!ledger->CheckDeadline() || !ledger->ChargeEdge(edge)) {
          result.incomplete = true;
          accept_payload = false;
          break;
        }
        if (edge_keys.insert(KeyFor(edge)).second) {
          result.graph.edges.push_back(std::move(edge));
        }
      }
    }

    if (accept_payload && !adapter_result.annotations.empty()) {
      // `node_index` is updated as every accepted node joins the graph. It
      // therefore has the same first-node semantics as rebuilding an index
      // here, without copying every identifier and walking the whole graph
      // once per annotating adapter.
      for (const NodeAnnotation& annotation : adapter_result.annotations) {
        if (!accept_payload || !ledger->CheckDeadline()) {
          result.incomplete = true;
          accept_payload = false;
          break;
        }
        auto it = node_index.find(annotation.node_id);
        if (it == node_index.end()) {
          continue;
        }
        if (!ledger->ChargeAnnotation(annotation)) {
          result.incomplete = true;
          accept_payload = false;
          break;
        }
        ApplyAnnotation(annotation, &result.graph.nodes[it->second]);
        if (entry.kind == AdapterKind::kSelection &&
            HasSelectedState(annotation.states)) {
          result.selection_node_ids.insert(annotation.node_id);
        }
      }
    }
    if (!accept_payload) {
      break;
    }
  }
  return result;
}

}  // namespace taffy
