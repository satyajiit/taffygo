// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/structured_data_adapter.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "taffy/renderer/adapters/structured_data_claims.h"
#include "taffy/renderer/adapters/structured_data_corroboration.h"
#include "taffy/renderer/adapters/structured_data_vocabulary.h"
#include "taffy/renderer/content_metadata.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"

// Reading the page's markup, with every bound that reading it needs, is
// structured_data_claims.{h,cc}. What is left here is the judgement: how much
// each claim is worth against what the page shows, which of them reach the
// graph, and what this adapter reports about itself.

namespace taffy {

using structured_data_claims::CollectedClaims;
using structured_data_claims::EntityScopedProperty;
using structured_data_claims::StructuredClaim;
using structured_data_corroboration::BuildPerceivableIndex;
using structured_data_corroboration::Corroboration;
using structured_data_corroboration::PerceivableIndex;
using structured_data_vocabulary::PageLabelsForProperty;
using structured_data_vocabulary::RoleForProperty;

namespace {

constexpr uint32_t kStructuredDataRuleVersion = 1;
constexpr char kAdapterName[] = "structured-data";

}  // namespace

StructuredDataAdapter::StructuredDataAdapter() = default;
StructuredDataAdapter::~StructuredDataAdapter() = default;

AdapterKind StructuredDataAdapter::kind() const {
  return AdapterKind::kStructuredData;
}

std::string_view StructuredDataAdapter::name() const {
  return kAdapterName;
}

uint32_t StructuredDataAdapter::extraction_rule_version() const {
  return kStructuredDataRuleVersion;
}

AdapterResult StructuredDataAdapter::Run(ExtractionContext& context) {
  AdapterResult result;

  const blink::WebDocument document = context.frame->GetDocument();
  if (document.IsNull()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-document");
    return result;
  }

  const StructuredDataLimits& limits = context.limits->structured_data();
  const ContentSignalLimits& signal_limits = context.limits->content_signals();
  const ContentSignalMask frame_signals = content_metadata::ContextSignals(
      /*hidden_by_style=*/false, /*language_mismatch=*/false,
      context.cross_origin_frame());
  CollectedClaims collected =
      structured_data_claims::Collect(context, document);
  std::vector<StructuredClaim>& candidates = collected.claims;

  if (candidates.empty()) {
    result.truncation = context.ledger->report();
    if (result.truncation.truncated) {
      result.status = AdapterStatus::kIncomplete;
      result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                   "structured-collection-truncated");
      return result;
    }
    // No structured data is not a failure and not an empty answer: it is this
    // adapter having nothing to say about this page (protocol section 7.6).
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-structured-data");
    return result;
  }

  // --- Freshness and visibility -------------------------------------------
  // The corroboration verdict per candidate, then the per-property verdict.
  const PerceivableIndex perceivable =
      BuildPerceivableIndex(context.accumulated, *context.ledger);

  // Values the page actually renders, grouped by entity-scoped property, so
  // that a structured claim can be told apart from a stale one rather than
  // merely from an absent one. Scoped rather than bare: see
  // EntityScopedProperty in structured_data_claims.h for why a bare property
  // name groups two different entities' claims into one false disagreement.
  std::map<std::string, std::vector<std::string>> claims_by_property;
  std::map<std::string, std::set<std::string>> rendered_by_property;
  std::vector<std::string> property_keys;
  std::vector<std::optional<SemanticNodeId>> rendered_matches;
  property_keys.reserve(candidates.size());
  rendered_matches.reserve(candidates.size());
  for (const StructuredClaim& candidate : candidates) {
    if (!context.ledger->CheckDeadline()) {
      break;
    }
    std::string key = EntityScopedProperty(candidate);
    claims_by_property[key].push_back(candidate.comparison_form);
    const std::optional<SemanticNodeId> match =
        perceivable.Find(candidate.comparison_form, *context.ledger);
    if (match.has_value()) {
      rendered_by_property[key].insert(candidate.comparison_form);
    }
    property_keys.push_back(std::move(key));
    rendered_matches.push_back(match);
  }

  bool conflicted = false;
  size_t judged_count = 0;
  for (size_t i = 0; i < rendered_matches.size(); ++i) {
    if (!context.ledger->CheckDeadline()) {
      break;
    }
    StructuredClaim& candidate = candidates[i];
    const std::optional<SemanticNodeId>& match = rendered_matches[i];
    if (match.has_value()) {
      candidate.corroboration = Corroboration::kCorroborated;
      candidate.corroborating_node = match;
      ++judged_count;
      continue;
    }
    // Not rendered. Two different things can make that a contradiction rather
    // than a value merely out of scope, and the page's own is the one the
    // protocol names: section 7.5 gives "a DOM-visible price and stale JSON-LD
    // price" as the example of conflicting evidence that must be preserved.
    //
    // The page states a value of its own for this property. That claim is
    // never a candidate here: the accessibility adapter emitted it, one step
    // above this adapter in the precedence order and before it in the run
    // order, which is exactly why `accumulated` is the place to look. So it is
    // found by asking the perceivable content whether a block that names this
    // property shows a different quantity.
    const std::optional<SemanticNodeId> disagreeing =
        perceivable.FindDisagreement(PageLabelsForProperty(candidate.property),
                                     candidate.comparison_form,
                                     *context.ledger);

    // Or ANOTHER structured candidate for the same property is rendered, which
    // makes this one the stale half of a pair the page itself resolved.
    const std::set<std::string>& rendered =
        rendered_by_property[property_keys[i]];
    const size_t self_is_rendered =
        rendered.contains(candidate.comparison_form) ? 1u : 0u;
    const bool a_sibling_is_rendered = rendered.size() > self_is_rendered;

    const bool contradicted = disagreeing.has_value() || a_sibling_is_rendered;
    candidate.corroboration = contradicted ? Corroboration::kContradicted
                                           : Corroboration::kUncorroborated;
    candidate.contradicting_node = disagreeing;
    conflicted |= contradicted;
    ++judged_count;
  }
  if (judged_count < candidates.size()) {
    context.ledger->NoteOmittedNodes(candidates.size() - judged_count,
                                     /*could_change_answer=*/true);
  }

  // Two structured sources stating different values for one property is also
  // a conflict, whether or not the page renders either of them.
  for (const auto& [property, forms] : claims_by_property) {
    if (!context.ledger->CheckDeadline()) {
      break;
    }
    if (forms.size() < 2) {
      continue;
    }
    const bool all_equal = std::ranges::all_of(
        forms, [&forms](const std::string& form) { return form == forms[0]; });
    conflicted |= !all_equal;
  }

  // --- Emit, preserving disagreement --------------------------------------
  std::map<std::string, std::vector<SemanticNodeId>> emitted_by_property;
  uint32_t derived_ordinal = 0;
  uint32_t dropped_over_property_limit = 0;

  for (size_t i = 0; i < judged_count; ++i) {
    StructuredClaim& candidate = candidates[i];
    const std::string& property_key = property_keys[i];
    if (emitted_by_property[property_key].size() >=
        limits.max_values_per_property()) {
      // Conflicting evidence is preserved, but not without a bound: a page
      // that publishes a thousand prices does not get a thousand nodes.
      ++dropped_over_property_limit;
      continue;
    }
    if (!context.ledger->ChargeNode()) {
      context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
      break;
    }
    if (context.prohibited_value_filter.LooksLikeHighRiskIdentifier(
            candidate.value)) {
      // A page can put a secret in a JSON-LD string. Dropped whole - a mask
      // would still state the length.
      context.ledger->NoteOmittedNode(/*could_change_answer=*/false);
      continue;
    }

    // A structured statement has no element of its own, so it is allocated in
    // the derived identity space. Nodes here are content and never action
    // targets: no actions are attached, and RendererActionExecutor refuses to
    // resolve a derived identity at all, so the promise is enforced rather
    // than merely intended.
    const SemanticNodeId id = context.store->AllocateOrLookup(
        context.store->MakeKey(SemanticGraphStore::IdentitySpace::kDerived,
                               static_cast<int64_t>(derived_ordinal++)));

    SemanticNode node;
    node.node_id = id;
    node.frame_id = context.store->frame_id();
    node.role = RoleForProperty(candidate.property);
    node.sensitivity = context.sensitivity_classifier.ClassifyContentRegion(
        candidate.property, context.cross_origin_frame(),
        context.policy_floor());
    node.name = candidate.property;
    const bool reader_authored =
        candidate.entity_type.find("Review") != std::string::npos ||
        candidate.entity_type.find("Comment") != std::string::npos;
    const RendererContentTrust content_trust =
        reader_authored
            ? content_metadata::UserGeneratedTrust(context.cross_origin_frame())
            : content_metadata::DocumentTrust(context.cross_origin_frame());
    content_metadata::ApplyNodeContext(&node, content_trust, frame_signals);
    content_metadata::AddNodeTextSignals(&node, candidate.property,
                                         frame_signals, signal_limits);

    const std::string locator =
        candidate.entity_type.empty()
            ? candidate.locator
            : candidate.locator + "#" + candidate.entity_type;

    TextRun run;
    run.text = context.ledger->BoundText(candidate.value);
    run.source_kind = candidate.source;
    run.sensitivity = node.sensitivity;
    content_metadata::LabelTextRun(&run, &node, content_trust, frame_signals,
                                   signal_limits);
    node.text_runs.push_back(std::move(run));

    // Marked kNormalized, never kNone: structured data is authored for
    // machines and is not what the user can see, so it is never presented as
    // observed fact.
    node.sources.push_back(candidate.source);
    node.evidence.push_back(MakeEvidence(SemanticField::kTextRuns,
                                         candidate.source, locator,
                                         Transformation::kNormalized));

    // The corroboration verdict is evidence about the value, with a
    // confidence that says how far it can be trusted. Neither the verdict nor
    // the confidence picks a winner.
    FieldEvidence freshness =
        MakeEvidence(SemanticField::kValueDescriptor, candidate.source, locator,
                     Transformation::kInferred);
    switch (candidate.corroboration) {
      case Corroboration::kCorroborated:
        freshness.confidence = 1.0;
        node.confidence = 1.0;
        break;
      case Corroboration::kUncorroborated:
        // Absent from the perceivable content this extraction collected. That
        // may only mean the content was out of scope, so it lowers confidence
        // rather than raising a conflict.
        freshness.confidence = 0.5;
        node.confidence = 0.5;
        break;
      case Corroboration::kContradicted:
        freshness.confidence = 0.0;
        node.confidence = 0.0;
        break;
    }
    node.evidence.push_back(std::move(freshness));

    if (candidate.authored_for_machines) {
      // Recorded so a consumer can tell a machine-only claim apart from one
      // the page also renders. A machine-only value is the kind that goes
      // stale without anybody noticing.
      node.evidence.push_back(MakeEvidence(SemanticField::kName,
                                           candidate.source, locator,
                                           Transformation::kNormalized));
    }

    if (candidate.corroborating_node.has_value()) {
      SemanticEdge edge;
      edge.from_frame_id = context.store->frame_id();
      edge.from_node_id = id;
      edge.to_frame_id = context.store->frame_id();
      edge.to_node_id = candidate.corroborating_node.value();
      edge.relationship = EdgeType::kSourceFor;
      edge.inferred = true;
      edge.confidence = 1.0;
      result.edges.push_back(std::move(edge));
    }

    if (candidate.contradicting_node.has_value()) {
      // The page's own value for this property is the other candidate, and
      // this is where the two are joined. Without the edge a consumer reads
      // 119.00 from the metadata and 129.00 from the page as two unrelated
      // facts; with it, they are one property with two candidates and a
      // confidence of zero on this one. Nothing here picks a winner, which is
      // the rule protocol section 7.6 states outright.
      SemanticEdge edge;
      edge.from_frame_id = context.store->frame_id();
      edge.from_node_id = candidate.contradicting_node.value();
      edge.to_frame_id = context.store->frame_id();
      edge.to_node_id = id;
      edge.relationship = EdgeType::kSameEntityAs;
      edge.inferred = true;
      result.edges.push_back(std::move(edge));
    }

    // Join every candidate for one property, so a consumer sees two prices
    // for one product rather than two unrelated prices.
    for (const SemanticNodeId& sibling : emitted_by_property[property_key]) {
      SemanticEdge edge;
      edge.from_frame_id = context.store->frame_id();
      edge.from_node_id = sibling;
      edge.to_frame_id = context.store->frame_id();
      edge.to_node_id = id;
      edge.relationship = EdgeType::kSameEntityAs;
      edge.inferred = true;
      result.edges.push_back(std::move(edge));
    }
    emitted_by_property[property_key].push_back(id);
    result.nodes.push_back(std::move(node));
  }

  result.truncation = context.ledger->report();
  if (collected.oversized_block_skipped) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "structured-block-over-size-bound");
  }
  if (collected.malformed_block_skipped) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "structured-block-malformed");
  }
  if (collected.block_limit_reached) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "structured-block-count-bound");
  }
  if (collected.claim_limit_reached) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "structured-claim-count-bound");
  }
  if (collected.traversal_limit_reached) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "structured-traversal-fanout-bound");
  }
  if (dropped_over_property_limit > 0) {
    result.warnings.emplace_back(WarningCode::kConflictingEvidence,
                                 "structured-candidates-over-bound");
  }
  if (!context.accumulated) {
    // Without the higher-precedence adapters' output there is nothing to
    // check freshness against, and a corroboration verdict nobody could have
    // computed must not be reported as one.
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-perceivable-content-to-corroborate");
  }
  if (conflicted) {
    result.warnings.emplace_back(WarningCode::kConflictingEvidence,
                                 "structured-data-disagrees-with-page");
    result.status = AdapterStatus::kConflicted;
    return result;
  }
  result.status =
      (result.truncation.truncated || collected.oversized_block_skipped ||
       collected.malformed_block_skipped || collected.block_limit_reached ||
       collected.claim_limit_reached || collected.traversal_limit_reached ||
       !context.accumulated)
          ? AdapterStatus::kIncomplete
          : AdapterStatus::kOk;
  return result;
}

}  // namespace taffy
