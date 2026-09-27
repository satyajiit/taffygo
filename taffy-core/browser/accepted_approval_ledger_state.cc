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

void AcceptedApprovalLedger::Reconcile(const CoreStateBindingRegistry& bindings,
                                       uint64_t service_generation,
                                       uint64_t now_monotonic_ms) {
  // Apply withdrawal facts before the ordinary projection reconciliation can
  // erase their browser-owned evidence. Pause retains one inert durable
  // consent; cancel and terminal states destroy it.
  for (const TaskSettlementLookup& settlement : bindings.task_settlements()) {
    ApplyTaskSettlement(settlement.task_id, settlement.service_generation,
                        settlement.kind);
  }
  for (const TerminalTaskLookup& terminal : bindings.terminal_tasks()) {
    RevokeTask(terminal.task_id, terminal.service_generation);
  }

  for (auto it = committed_approvals_.begin();
       it != committed_approvals_.end();) {
    const ApprovalRecord& record = it->second;
    const std::optional<uint64_t> revision =
        bindings.FindTaskRevision(record.submission.task_id);
    if (record.submission.service_generation != service_generation ||
        record.expires_at_monotonic_ms <= now_monotonic_ms || !revision ||
        *revision != record.submission.resulting_revision ||
        bindings.FindPendingApproval(record.submission.task_id,
                                     record.action_id) ||
        HasTaskSettlement(bindings, record.submission.task_id,
                          service_generation) ||
        HasTerminalTask(bindings, record.submission.task_id,
                        service_generation)) {
      it = committed_approvals_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = staged_approvals_.begin(); it != staged_approvals_.end();) {
    ApprovalRecord& record = it->second;
    const std::optional<uint64_t> revision =
        bindings.FindTaskRevision(record.submission.task_id);
    if (record.submission.service_generation != service_generation ||
        record.expires_at_monotonic_ms <= now_monotonic_ms || !revision ||
        HasTaskSettlement(bindings, record.submission.task_id,
                          service_generation) ||
        HasTerminalTask(bindings, record.submission.task_id,
                        service_generation)) {
      it = staged_approvals_.erase(it);
      continue;
    }
    if (!record.submission.storage_committed) {
      if (*revision != record.submission.submitted_revision) {
        it = staged_approvals_.erase(it);
      } else {
        ++it;
      }
      continue;
    }
    if (*revision == record.submission.submitted_revision) {
      const std::optional<PendingApprovalLookup> pending =
          bindings.FindPendingApproval(record.submission.task_id,
                                       record.action_id);
      if (!pending ||
          pending->service_generation != record.submission.service_generation ||
          pending->task_revision != record.submission.submitted_revision ||
          pending->proposal_digest != record.proposal_digest) {
        it = staged_approvals_.erase(it);
      } else {
        ++it;
      }
      continue;
    }
    if (*revision != record.submission.resulting_revision ||
        bindings.FindPendingApproval(record.submission.task_id,
                                     record.action_id)) {
      it = staged_approvals_.erase(it);
      continue;
    }

    ApprovalKey key(record.submission.task_id, record.action_id);
    if (committed_approvals_.contains(key)) {
      it = staged_approvals_.erase(it);
      committed_approvals_.erase(key);
      continue;
    }
    committed_approvals_.emplace(key, std::move(record));
    it = staged_approvals_.erase(it);
  }

  for (auto it = accepted_consents_.begin(); it != accepted_consents_.end();) {
    const ConsentRecord& record = it->second;
    const std::optional<uint64_t> revision =
        bindings.FindTaskRevision(record.submission.task_id);
    // Losing this record ends a task without ending it: every later proposal
    // is refused `kEgressNotAuthorized` for a tab the task is standing in, and
    // the only evidence was four zeroes in the source refusal. Five clauses,
    // all of them counters and flags (decision 0177).
    const char* at = nullptr;
    if (record.submission.service_generation != service_generation) {
      at = "generation";
    } else if (!revision) {
      at = "task-not-published";
    } else if (*revision < record.submission.resulting_revision) {
      at = "revision-behind";
    } else if (HasTaskSettlement(bindings, record.submission.task_id,
                                 service_generation)) {
      at = "settled";
    } else if (HasTerminalTask(bindings, record.submission.task_id,
                               service_generation)) {
      at = "terminal";
    }
    if (at) {
      LOG(WARNING) << "[taffy_task_consent_dropped] at=" << at
                   << " published=" << (revision ? *revision : 0u)
                   << " accepted=" << record.submission.resulting_revision;
      it = accepted_consents_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = staged_consents_.begin(); it != staged_consents_.end();) {
    ConsentRecord& record = it->second;
    const std::optional<uint64_t> revision =
        bindings.FindTaskRevision(record.submission.task_id);
    if (record.submission.service_generation != service_generation ||
        HasTaskSettlement(bindings, record.submission.task_id,
                          service_generation) ||
        HasTerminalTask(bindings, record.submission.task_id,
                        service_generation)) {
      it = staged_consents_.erase(it);
      continue;
    }
    if (!record.submission.storage_committed) {
      if (revision) {
        it = staged_consents_.erase(it);
      } else {
        ++it;
      }
      continue;
    }
    if (!revision) {
      ++it;
      continue;
    }
    // The core can complete more durable setup before publishing its first
    // post-creation snapshot. Keep the browser's completed candidate until
    // RehydrateDurableAuthority checks the accepted-revision binding in this
    // same snapshot; a missing or mutated binding revokes it there.
    if (*revision > record.submission.resulting_revision) {
      ++it;
      continue;
    }
    if (*revision != record.submission.resulting_revision ||
        accepted_consents_.contains(record.submission.task_id)) {
      it = staged_consents_.erase(it);
      continue;
    }

    const std::string task_id = record.submission.task_id;
    accepted_consents_.emplace(task_id, std::move(record));
    it = staged_consents_.erase(it);
  }
}

void AcceptedApprovalLedger::ResetForServiceGenerationLoss() {
  const auto durable = [](const auto& record) {
    return record.submission.storage_bound &&
           record.submission.storage_committed &&
           record.submission.resulting_revision >
               record.submission.submitted_revision;
  };
  auto retain_consents = [this, &durable](auto* records) {
    for (auto& [task_id, record] : *records) {
      record.pending_replacements.clear();
      if (durable(record)) {
        generation_loss_consent_candidates_.insert_or_assign(task_id,
                                                             std::move(record));
      }
    }
  };
  auto retain_approvals = [this, &durable](auto* records) {
    for (auto& [key, record] : *records) {
      if (durable(record)) {
        generation_loss_approval_candidates_.insert_or_assign(
            key, std::move(record));
      }
    }
  };
  retain_consents(&staged_consents_);
  retain_consents(&accepted_consents_);
  retain_approvals(&staged_approvals_);
  retain_approvals(&committed_approvals_);
  staged_approvals_.clear();
  committed_approvals_.clear();
  staged_consents_.clear();
  accepted_consents_.clear();
  for (auto& entry : suspended_consents_) {
    SuspendedConsentRecord& record = entry.second;
    record.consent.pending_replacements.clear();
    if (record.resume_submission &&
        (!record.resume_submission->storage_bound ||
         !record.resume_submission->storage_committed ||
         record.resume_submission->resulting_revision <=
             record.resume_submission->submitted_revision)) {
      record.resume_submission.reset();
    }
  }
}

void AcceptedApprovalLedger::Reset() {
  staged_approvals_.clear();
  committed_approvals_.clear();
  staged_consents_.clear();
  accepted_consents_.clear();
  generation_loss_approval_candidates_.clear();
  generation_loss_consent_candidates_.clear();
  suspended_consents_.clear();
}

}  // namespace taffy
