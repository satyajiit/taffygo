// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/containers/flat_set.h"
#include "base/logging.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_api/task_consent_shape.h"
#include "taffy/browser/core_state_binding_registry.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool HasTaskSettlement(const CoreStateBindingRegistry& bindings,
                       const std::string& task_id,
                       uint64_t service_generation) {
  return std::any_of(
      bindings.task_settlements().begin(), bindings.task_settlements().end(),
      [&task_id, service_generation](const TaskSettlementLookup& settlement) {
        return settlement.task_id == task_id &&
               settlement.service_generation == service_generation;
      });
}

bool HasTerminalTask(const CoreStateBindingRegistry& bindings,
                     const std::string& task_id,
                     uint64_t service_generation) {
  return std::any_of(
      bindings.terminal_tasks().begin(), bindings.terminal_tasks().end(),
      [&task_id, service_generation](const TerminalTaskLookup& terminal) {
        return terminal.task_id == task_id &&
               terminal.service_generation == service_generation;
      });
}

}  // namespace

void AcceptedApprovalLedger::RehydrateDurableAuthority(
    const mojom::CoreStateBrowserBindings& bindings,
    const CoreStateBindingRegistry& validated_bindings,
    uint64_t service_generation,
    const std::string& browser_session_id,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms,
    base::RepeatingCallback<IssuedSourceLiveness(
        const mojom::TaskConsentSource&)> live_source) {
  if (bindings.service_generation != service_generation ||
      validated_bindings.service_generation() != service_generation ||
      bindings.state_sequence != validated_bindings.state_sequence() ||
      browser_session_id.empty() || !live_source) {
    Reset();
    return;
  }

  // A generation may die after PauseSettled reached durable storage but
  // before the browser observed the earlier Pausing settlement snapshot. The
  // restored core then publishes Paused directly: its exact Resume control is
  // the inert state marker, while this ledger's completed consent remains the
  // only authority proof. Move that proof into the pause slot before ordinary
  // reconciliation can discard it. An existing pause slot is left untouched
  // so a later Paused snapshot cannot erase an in-flight Resume operation.
  base::flat_set<std::string> paused_candidate_tasks;
  const auto note_paused_candidate =
      [&validated_bindings, &paused_candidate_tasks](const auto& entry) {
        if (validated_bindings.FindTaskControl(
                entry.first, mojom::TaskControlKind::kResume)) {
          paused_candidate_tasks.insert(entry.first);
        }
      };
  for (const auto& entry : accepted_consents_) {
    note_paused_candidate(entry);
  }
  for (const auto& entry : staged_consents_) {
    note_paused_candidate(entry);
  }
  for (const auto& entry : generation_loss_consent_candidates_) {
    note_paused_candidate(entry);
  }
  for (const std::string& task_id : paused_candidate_tasks) {
    if (!suspended_consents_.contains(task_id)) {
      ApplyTaskSettlement(task_id, service_generation,
                          mojom::TaskSettlementKind::kPause);
    }
  }

  base::flat_map<std::string, ConsentRecord> next_consents;
  base::flat_map<ApprovalKey, ApprovalRecord> next_approvals;
  base::flat_set<std::string> restored_suspended_consents;

  const auto find_consent_candidate = [this, service_generation](
                                          const std::string& task_id,
                                          uint64_t current_task_revision)
      -> std::pair<const ConsentRecord*, bool> {
    const auto active = accepted_consents_.find(task_id);
    const auto staged = staged_consents_.find(task_id);
    const auto retained = generation_loss_consent_candidates_.find(task_id);
    const size_t ordinary_candidate_count =
        static_cast<size_t>(active != accepted_consents_.end()) +
        static_cast<size_t>(staged != staged_consents_.end()) +
        static_cast<size_t>(retained !=
                            generation_loss_consent_candidates_.end());
    const auto suspended = suspended_consents_.find(task_id);
    if (suspended != suspended_consents_.end()) {
      const SuspendedConsentRecord& record = suspended->second;
      const SubmissionRecord* resume =
          record.resume_submission ? &*record.resume_submission : nullptr;
      const bool resume_ready =
          resume && resume->storage_bound && resume->storage_committed &&
          resume->task_id == task_id && resume->service_generation != 0u &&
          resume->service_generation <= service_generation &&
          resume->resulting_revision > resume->submitted_revision &&
          resume->submitted_revision >=
              record.consent.submission.resulting_revision &&
          current_task_revision >= resume->resulting_revision &&
          record.consent.submission.service_generation <= service_generation;
      if (ordinary_candidate_count != 0u || !resume_ready) {
        return {nullptr, false};
      }
      return {&record.consent, true};
    }
    if (ordinary_candidate_count != 1u) {
      return {nullptr, false};
    }
    if (active != accepted_consents_.end()) {
      return active->second.submission.service_generation == service_generation
                 ? std::pair<const ConsentRecord*, bool>{&active->second, false}
                 : std::pair<const ConsentRecord*, bool>{nullptr, false};
    }
    if (staged != staged_consents_.end()) {
      return staged->second.submission.service_generation == service_generation
                 ? std::pair<const ConsentRecord*, bool>{&staged->second, false}
                 : std::pair<const ConsentRecord*, bool>{nullptr, false};
    }
    if (retained != generation_loss_consent_candidates_.end()) {
      return retained->second.submission.service_generation < service_generation
                 ? std::pair<const ConsentRecord*, bool>{&retained->second,
                                                         false}
                 : std::pair<const ConsentRecord*, bool>{nullptr, false};
    }
    return {nullptr, false};
  };
  const auto find_approval_candidate =
      [this,
       service_generation](const ApprovalKey& key) -> const ApprovalRecord* {
    const auto active = committed_approvals_.find(key);
    const auto retained = generation_loss_approval_candidates_.find(key);
    if (active != committed_approvals_.end() &&
        retained != generation_loss_approval_candidates_.end()) {
      return nullptr;
    }
    if (active != committed_approvals_.end()) {
      return active->second.submission.service_generation == service_generation
                 ? &active->second
                 : nullptr;
    }
    if (retained != generation_loss_approval_candidates_.end()) {
      return retained->second.submission.service_generation < service_generation
                 ? &retained->second
                 : nullptr;
    }
    return nullptr;
  };

  for (const auto& binding : bindings.accepted_task_consents) {
    if (!binding) {
      continue;
    }
    const auto [candidate, resumes_suspended] = find_consent_candidate(
        binding->task_id, binding->current_task_revision);
    if (!candidate || !candidate->submission.storage_bound ||
        !candidate->submission.storage_committed ||
        candidate->submission.task_id != binding->task_id ||
        candidate->submission.resulting_revision !=
            binding->accepted_revision ||
        candidate->receipt_reference != binding->receipt_id ||
        candidate->browser_session_id != browser_session_id ||
        binding->service_generation != service_generation ||
        binding->browser_session_id != browser_session_id ||
        !binding->consent_preview ||
        !IsAdmittedDurableTaskConsentShape(*binding->consent_preview) ||
        validated_bindings.FindTaskRevision(binding->task_id) !=
            std::optional<uint64_t>(binding->current_task_revision) ||
        binding->accepted_revision == 0u ||
        binding->accepted_revision > binding->current_task_revision ||
        HasTaskSettlement(validated_bindings, binding->task_id,
                          service_generation) ||
        HasTerminalTask(validated_bindings, binding->task_id,
                        service_generation)) {
      // The other half of the same fact `RehydrateConsentSources` logs: a
      // published consent binding this browser cannot match is a task about to
      // lose every move it has left, silently (decision 0179).
      LOG(WARNING) << "[taffy_task_consent_not_rehydrated] at=binding"
                   << " candidate=" << (candidate ? 1 : 0)
                   << " accepted=" << binding->accepted_revision
                   << " current=" << binding->current_task_revision;
      continue;
    }

    auto restored = RehydrateConsentSources(*candidate, *binding, live_source);
    if (restored) {
      restored->submission.service_generation = service_generation;
      next_consents.emplace(binding->task_id, std::move(*restored));
      if (resumes_suspended) {
        restored_suspended_consents.insert(binding->task_id);
      }
    }
  }

  for (const auto& binding : bindings.committed_action_approvals) {
    if (!binding) {
      continue;
    }
    const ApprovalKey key(binding->task_id, binding->action_id);
    const ApprovalRecord* candidate = find_approval_candidate(key);
    if (!candidate || !candidate->submission.storage_bound ||
        !candidate->submission.storage_committed ||
        candidate->submission.task_id != binding->task_id ||
        candidate->submission.resulting_revision !=
            binding->committed_revision ||
        candidate->action_id != binding->action_id ||
        candidate->receipt_reference != binding->receipt_id ||
        candidate->proposal_digest != binding->proposal_digest ||
        candidate->expires_at_monotonic_ms !=
            binding->expires_at_monotonic_ms ||
        candidate->expires_at_utc_ms != binding->expires_at_utc_ms ||
        candidate->browser_session_id != browser_session_id ||
        binding->service_generation != service_generation ||
        binding->browser_session_id != browser_session_id ||
        binding->expires_at_monotonic_ms <= now_monotonic_ms ||
        binding->expires_at_utc_ms <= now_utc_ms ||
        validated_bindings.FindTaskRevision(binding->task_id) !=
            std::optional<uint64_t>(binding->committed_revision) ||
        validated_bindings.FindPendingApproval(binding->task_id,
                                               binding->action_id) ||
        HasTaskSettlement(validated_bindings, binding->task_id,
                          service_generation) ||
        HasTerminalTask(validated_bindings, binding->task_id,
                        service_generation)) {
      continue;
    }

    ApprovalRecord restored = *candidate;
    restored.submission.service_generation = service_generation;
    next_approvals.emplace(key, std::move(restored));
  }

  accepted_consents_ = std::move(next_consents);
  committed_approvals_ = std::move(next_approvals);
  generation_loss_consent_candidates_.clear();
  generation_loss_approval_candidates_.clear();

  for (auto it = suspended_consents_.begin();
       it != suspended_consents_.end();) {
    if (restored_suspended_consents.contains(it->first)) {
      it = suspended_consents_.erase(it);
      continue;
    }
    const std::optional<uint64_t> revision =
        validated_bindings.FindTaskRevision(it->first);
    if (!revision) {
      it = suspended_consents_.erase(it);
      continue;
    }
    SubmissionRecord* resume =
        it->second.resume_submission ? &*it->second.resume_submission : nullptr;
    if (!resume) {
      ++it;
      continue;
    }
    if (!resume->storage_committed) {
      if (resume->service_generation != service_generation ||
          *revision != resume->submitted_revision ||
          resume->deadline_monotonic_ms <= now_monotonic_ms) {
        it->second.resume_submission.reset();
      }
      ++it;
      continue;
    }
    if (resume->service_generation == 0u ||
        resume->service_generation > service_generation ||
        resume->resulting_revision <= resume->submitted_revision ||
        *revision >= resume->resulting_revision) {
      // This state has reached the durable Resume transition. The exact
      // consent above either reactivated it or was absent/mutated, which
      // authoritatively closes the retained proof.
      it = suspended_consents_.erase(it);
      continue;
    }
    ++it;
  }

  for (auto it = staged_consents_.begin(); it != staged_consents_.end();) {
    // A full state produced before task creation carries no task watermark. It
    // can cross the independent storage/publication pipes after the creation
    // terminal, but cannot revoke the browser-held consent proof for a revision
    // it does not yet include. Keep that proof staged and inert. A state which
    // has reached the resulting revision either rehydrated the exact consent
    // above or authoritatively omitted/mutated it, so the staged copy ends
    // here.
    const auto revision = validated_bindings.FindTaskRevision(it->first);
    if (!it->second.submission.storage_committed || !revision ||
        *revision < it->second.submission.resulting_revision) {
      ++it;
      continue;
    }
    it = staged_consents_.erase(it);
  }
  // Reconcile deliberately leaves a completed approval staged while the
  // accepted snapshot still names the revision and pending approval that the
  // person acted on. An older state may cross the independent storage and
  // publication pipes after the commit terminal; erasing here would discard
  // the browser-owned half before the commit's own state can acknowledge it.
  // Once the task revision changes, Reconcile either promotes the exact
  // committed tuple or revokes it closed.
}

}  // namespace taffy
