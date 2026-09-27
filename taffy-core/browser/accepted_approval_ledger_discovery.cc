// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>

#include "base/containers/flat_set.h"
#include "base/logging.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/accepted_approval_ledger_shapes.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using accepted_approval_ledger_shapes::IsIdentifier;
using accepted_approval_ledger_shapes::IsNormalizedTupleOrigin;

}  // namespace

bool AcceptedApprovalLedger::HasTaskSourceDiscoveryBootstrapAuthority(
    const std::string& task_id,
    const mojom::OperationEnvelope& operation,
    uint64_t service_generation,
    const std::string& browser_session_id,
    uint32_t remaining_new_source_cap) const {
  const auto consent = accepted_consents_.find(task_id);
  if (consent == accepted_consents_.end()) {
    return false;
  }
  const ConsentRecord& record = consent->second;
  // Holding a source is not a reason to refuse discovery. The sheet grants
  // the two separately — "read this page" and "you may open up to N sites" —
  // and requiring an empty source map here made the second grant unspendable
  // for the ordinary errand, the one a person asks from a page they are on.
  // What still bounds this is the cap: the record's own remaining count must
  // equal what the core asked with, and it falls as sources are admitted, so
  // a task that has spent its allowance has no discovery authority left
  // (decision 0224).
  return record.submission.service_generation == service_generation &&
         operation.service_generation == service_generation &&
         operation.task_revision >= record.submission.resulting_revision &&
         record.browser_session_id == browser_session_id &&
         record.source_discovery_enabled && remaining_new_source_cap > 0u &&
         record.remaining_new_source_cap == remaining_new_source_cap;
}

TaskSourceDiscoveryVerdict AcceptedApprovalLedger::ValidateDiscoveredTaskSource(
    const std::string& task_id,
    const mojom::OperationEnvelope& operation,
    const mojom::TaskConsentSource& source,
    uint64_t service_generation,
    base::RepeatingCallback<bool(const mojom::TaskConsentSource&)>
        live_source) {
  const auto consent = accepted_consents_.find(task_id);
  if (consent == accepted_consents_.end() || !live_source ||
      !IsIdentifier(source.source_id) || !IsIdentifier(source.tab_id) ||
      !IsNormalizedTupleOrigin(source.normalized_origin)) {
    return TaskSourceDiscoveryVerdict::kRefused;
  }
  ConsentRecord& record = consent->second;
  if (record.submission.service_generation != service_generation ||
      operation.service_generation != service_generation ||
      operation.task_revision < record.submission.resulting_revision ||
      !record.source_discovery_enabled || !live_source.Run(source)) {
    return TaskSourceDiscoveryVerdict::kRefused;
  }

  for (const auto& [tab_id, existing] : record.sources_by_tab) {
    const bool same_source_id = existing.source_id == source.source_id;
    const bool same_tab = tab_id == source.tab_id;
    const bool exact = same_source_id && same_tab &&
                       existing.normalized_origin == source.normalized_origin &&
                       existing.canonical_locator == source.canonical_locator;
    if (exact) {
      return TaskSourceDiscoveryVerdict::kAlreadyBound;
    }
    // A source identity is immutable. A tab may acquire a replacement source
    // after a verified cross-origin discovery, but the browser must issue a
    // new opaque source id for that new tuple.
    if (same_source_id ||
        (same_tab && existing.normalized_origin == source.normalized_origin)) {
      return TaskSourceDiscoveryVerdict::kRefused;
    }
  }
  if (record.remaining_new_source_cap == 0u) {
    return TaskSourceDiscoveryVerdict::kRefused;
  }
  const auto previous = record.sources_by_tab.find(source.tab_id);
  if (previous != record.sources_by_tab.end()) {
    const auto pending = record.pending_replacements.find(source.tab_id);
    if (pending != record.pending_replacements.end()) {
      const SourceReplacementRecord& held = pending->second;
      return held.operation_id == operation.operation_id &&
                     held.service_generation == operation.service_generation &&
                     held.submitted_revision == operation.task_revision &&
                     held.previous_source_id == previous->second.source_id &&
                     held.source.source_id == source.source_id &&
                     held.source.normalized_origin ==
                         source.normalized_origin &&
                     held.source.canonical_locator == source.canonical_locator
                 ? TaskSourceDiscoveryVerdict::kAdmitted
                 : TaskSourceDiscoveryVerdict::kRefused;
    }
    record.pending_replacements.emplace(
        source.tab_id,
        SourceReplacementRecord{
            ConsentSourceRecord{source.source_id, source.normalized_origin,
                                source.canonical_locator},
            previous->second.source_id, operation.operation_id,
            operation.service_generation, operation.task_revision});
  }
  return TaskSourceDiscoveryVerdict::kAdmitted;
}

std::optional<AcceptedApprovalLedger::ConsentRecord>
AcceptedApprovalLedger::RehydrateConsentSources(
    const ConsentRecord& candidate,
    const mojom::AcceptedTaskConsentBinding& binding,
    base::RepeatingCallback<IssuedSourceLiveness(
        const mojom::TaskConsentSource&)> live_source) const {
  // Every refusal below destroys the task's standing consent, and the task is
  // told nothing: its next proposal is refused `kEgressNotAuthorized` with
  // four zeroes, on a page it is standing in. Losing a record is a fact worth
  // one line (decision 0179, register entry OD-144).
  const auto refuse = [&binding](const char* at) {
    LOG(WARNING) << "[taffy_task_consent_not_rehydrated] at=" << at
                 << " accepted=" << binding.accepted_revision
                 << " current=" << binding.current_task_revision;
    return std::nullopt;
  };
  const auto& preview = *binding.consent_preview;
  if (candidate.source_discovery_enabled != preview.source_discovery_enabled ||
      candidate.provider_route != preview.provider_route ||
      candidate.source_order.size() != candidate.sources_by_tab.size() ||
      preview.sources.size() < candidate.source_order.size()) {
    return refuse("preview-shape");
  }
  const auto same_source = [](const ConsentSourceRecord& held,
                              const mojom::TaskConsentSource& source) {
    return held.source_id == source.source_id &&
           held.normalized_origin == source.normalized_origin &&
           held.canonical_locator == source.canonical_locator;
  };
  size_t replacements = 0u;
  base::flat_set<std::string> source_ids;
  base::flat_set<std::string> tab_ids;
  base::flat_set<std::string> replaced_tabs;
  std::vector<std::string> unchanged_order;
  for (const auto& source : preview.sources) {
    if (!source || !source_ids.insert(source->source_id).second ||
        !tab_ids.insert(source->tab_id).second) {
      return refuse("source-shape");
    }
    const auto expected = candidate.sources_by_tab.find(source->tab_id);
    // The one clause an ordinary errand reaches, and the one that used to take
    // a task's every remaining move. Refusing here destroys the record, and
    // destroying it is permanent: the candidate the next pass looks for lives
    // in the very map this pass rebuilds, so one miss is the last word. Two
    // phones measured the same shape — one `at=source-not-live`, then dozens
    // of `at=binding candidate=0`, while every later proposal, including every
    // search, came back `kEgressNotAuthorized`. The task was not looping; it
    // was proposing sensible moves into a browser that had forgotten it had
    // consent.
    //
    // So the middle answer is tolerated, and only here. A tab showing an error
    // document is a tab whose source nobody can currently confirm — not a tab
    // whose source is wrong. Keeping a record the ledger already holds through
    // that is a refusal deferred, not authority widened: the source, origin
    // and locator must match what is already in `sources_by_tab` exactly, so
    // the rebuilt record is byte-for-byte the one that was already accepted.
    // Minting a source for a document nobody can observe stays refused, which
    // is what `DeadOrUnknownDiscoveredSourceCannotExtendAuthority` asks, and a
    // tab a person carried to another site answers `kGone` rather than this,
    // which is what `ErrandConsentRefusesManualCrossOriginNavigation` and
    // `ResearchConsentRemainsBoundToReviewedPage` ask (decision 0183).
    const bool refreshes_a_source_already_held =
        expected != candidate.sources_by_tab.end() &&
        same_source(expected->second, *source);
    switch (live_source.Run(*source)) {
      case IssuedSourceLiveness::kLive:
        break;
      case IssuedSourceLiveness::kTabHasNoDocumentOfItsOwn:
        if (!refreshes_a_source_already_held) {
          return refuse("new-source-not-live");
        }
        // Said out loud, because a record kept is as worth reading as a record
        // lost and this is the line that will tell the next person the rule
        // fired rather than that nothing happened. It is also the only signal
        // that a tab has been sitting on a document with no site across many
        // publications, which nothing bounds.
        LOG(WARNING) << "[taffy_task_consent_held_through_no_site]"
                     << " accepted=" << binding.accepted_revision
                     << " current=" << binding.current_task_revision;
        break;
      case IssuedSourceLiveness::kGone:
        return refuse("source-not-live");
    }
    if (expected == candidate.sources_by_tab.end()) {
      if (std::any_of(candidate.sources_by_tab.begin(),
                      candidate.sources_by_tab.end(), [&](const auto& entry) {
                        return entry.second.source_id == source->source_id;
                      })) {
        return refuse("source-moved-tab");
      }
      continue;
    }
    if (same_source(expected->second, *source)) {
      unchanged_order.push_back(source->tab_id);
      continue;
    }
    const auto pending = candidate.pending_replacements.find(source->tab_id);
    if (pending == candidate.pending_replacements.end() ||
        pending->second.service_generation != binding.service_generation ||
        pending->second.submitted_revision >= binding.current_task_revision ||
        pending->second.previous_source_id != expected->second.source_id ||
        !same_source(pending->second.source, *source)) {
      return refuse("replacement-unmatched");
    }
    replaced_tabs.insert(source->tab_id);
    ++replacements;
  }
  std::vector<std::string> expected_unchanged_order;
  for (const auto& tab : candidate.source_order) {
    if (!tab_ids.contains(tab)) {
      return refuse("tab-dropped");
    }
    if (!replaced_tabs.contains(tab)) {
      expected_unchanged_order.push_back(tab);
    }
  }
  // Rust sorts after both additions and replacements. New source identities
  // may land before existing rows; only unchanged rows retain relative order.
  if (unchanged_order != expected_unchanged_order) {
    return refuse("source-order");
  }
  const size_t spent =
      preview.sources.size() - candidate.source_order.size() + replacements;
  if (spent > candidate.remaining_new_source_cap ||
      preview.new_source_cap != candidate.remaining_new_source_cap - spent) {
    return refuse("sites-budget");
  }

  ConsentRecord restored = candidate;
  restored.source_order.clear();
  restored.sources_by_tab.clear();
  for (const auto& source : preview.sources) {
    restored.source_order.push_back(source->tab_id);
    restored.sources_by_tab.emplace(
        source->tab_id,
        ConsentSourceRecord{source->source_id, source->normalized_origin,
                            source->canonical_locator});
  }
  restored.remaining_new_source_cap = preview.new_source_cap;
  for (auto it = restored.pending_replacements.begin();
       it != restored.pending_replacements.end();) {
    if (it->second.service_generation != binding.service_generation ||
        it->second.submitted_revision < binding.current_task_revision) {
      it = restored.pending_replacements.erase(it);
    } else {
      ++it;
    }
  }
  return restored;
}

}  // namespace taffy
