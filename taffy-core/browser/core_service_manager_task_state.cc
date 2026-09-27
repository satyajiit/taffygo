// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/policy_response_validation.h"
#include "taffy/browser/profile_tool_artifact_broker.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

std::string TaskSettlementKey(const TaskSettlementLookup& settlement) {
  return settlement.task_id + ":" +
         std::to_string(settlement.service_generation) + ":" +
         std::to_string(settlement.task_revision) + ":" +
         std::to_string(static_cast<uint32_t>(settlement.kind));
}

ProfileToolArtifactBroker::TaskDisposition TerminalDisposition(
    service_mojom::TerminalTaskKind kind) {
  switch (kind) {
    case service_mojom::TerminalTaskKind::kCompleted:
    case service_mojom::TerminalTaskKind::kPartial:
      return ProfileToolArtifactBroker::TaskDisposition::kFinished;
    case service_mojom::TerminalTaskKind::kFailed:
    case service_mojom::TerminalTaskKind::kCancelled:
      return ProfileToolArtifactBroker::TaskDisposition::kAbandoned;
  }
  return ProfileToolArtifactBroker::TaskDisposition::kAbandoned;
}

}  // namespace

void CoreServiceManager::RegisterCapability(
    service_mojom::MintedCapabilityGrantPtr grant,
    RegisterCapabilityCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  service_mojom::CapabilityRegistrationStatus status =
      service_mojom::CapabilityRegistrationStatus::kInvalidGrant;
  if (!grant || availability_ != Availability::kReady) {
    if (grant && grant->service_generation != service_generation_) {
      status = service_mojom::CapabilityRegistrationStatus::kStaleGeneration;
    }
  } else {
    const service_mojom::PolicyEvaluationRequest* request =
        FindPendingPolicyRequestForGrant(*grant);
    if (request && PolicyGrantMatchesRequest(*grant, *request)) {
      status =
          capabilities_.Register(*grant, actor_leases_, base::TimeTicks::Now());
    } else {
      LOG(WARNING) << "[taffy_capability_registration_refused] at=match"
                   << " have_request=" << (request ? 1 : 0);
    }
  }
  if (status != service_mojom::CapabilityRegistrationStatus::kRegistered) {
    // Refusing here discards a grant the ordered core already minted, and the
    // utility turns that into INVALID_REQUEST, which the task engine records
    // as `Deny(Unsupported)`. Nothing on that whole path said a word.
    LOG(WARNING) << "[taffy_capability_registration_refused] status="
                 << static_cast<int>(status)
                 << " op="
                 << (grant ? static_cast<int>(grant->operation_kind) : -1)
                 << " dest_addr="
                 << (grant && grant->scope && grant->scope->destination_address
                         ? *grant->scope->destination_address
                         : std::string());
  }
  std::move(callback).Run(status);
}

void CoreServiceManager::RegisterPendingApprovals(
    service_mojom::CoreStateBrowserBindingsPtr bindings,
    RegisterPendingApprovalsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  service_mojom::PendingApprovalRegistrationStatus status =
      service_mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
  if (bindings && bindings->service_generation != service_generation_) {
    status = service_mojom::PendingApprovalRegistrationStatus::kStaleGeneration;
  } else if (bindings &&
             (availability_ == Availability::kStarting ||
              availability_ == Availability::kReady) &&
             state_cache_.IsNextSequence(bindings->state_sequence) &&
             state_bindings_.state_sequence() != bindings->state_sequence) {
    status = state_bindings_.Replace(*bindings);
    if (status ==
        service_mojom::PendingApprovalRegistrationStatus::kRegistered) {
      accepted_approvals_.Reconcile(state_bindings_, service_generation_,
                                    NowMonotonicMillis());
      accepted_approvals_.RehydrateDurableAuthority(
          *bindings, state_bindings_, service_generation_, browser_session_id_,
          NowMonotonicMillis(), NowUtcMillis(), live_task_source_validator_);
      for (auto it = emitted_permission_requests_.begin();
           it != emitted_permission_requests_.end();) {
        if (state_bindings_.FindPendingPermission(*it)) {
          ++it;
        } else {
          it = emitted_permission_requests_.erase(it);
        }
      }
      // A completed identity is a replay guard for a settlement still named
      // by the current state, not a browser-session history. Intersecting at
      // each accepted snapshot bounds this set by the contract's settlement
      // cap while preserving deduplication until the core publishes removal.
      base::flat_set<std::string> active_settlements;
      for (const TaskSettlementLookup& settlement :
           state_bindings_.task_settlements()) {
        active_settlements.insert(TaskSettlementKey(settlement));
      }
      for (auto it = completed_task_settlements_.begin();
           it != completed_task_settlements_.end();) {
        if (active_settlements.contains(*it)) {
          ++it;
        } else {
          it = completed_task_settlements_.erase(it);
        }
      }
      RevokeRegisteredTerminalTasks();
      CompleteRegisteredTaskSettlements();
    }
  } else if (bindings) {
    status = service_mojom::PendingApprovalRegistrationStatus::kStaleSequence;
  }
  std::move(callback).Run(status);
}

void CoreServiceManager::PublishTaskAnswerEvents(
    std::vector<service_mojom::TaskAnswerEventPtr> events,
    PublishTaskAnswerEventsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (availability_ != Availability::kReady || events.empty() ||
      events.size() > service_mojom::kMaxTaskAnswerEventsPerBatch) {
    std::move(callback).Run(false);
    return;
  }

  // Validate the complete batch against an overlay containing only the keys
  // it touches. A malformed later event must not make an earlier prefix
  // visible or advance the sequence ledger, but a small stream batch must not
  // copy every live and completed call in the profile to get that guarantee.
  base::flat_map<TaskAnswerKey, TaskAnswerSequenceState> staged_sequences;
  std::vector<TaskAnswerKey> staged_completions;
  staged_completions.reserve(events.size());
  size_t active_calls = active_task_answer_count_;
  for (const auto& event : events) {
    if (!event || event->task_id.empty() ||
        event->task_id.size() > service_mojom::kMaxIdentifierBytes ||
        event->call_id.empty() ||
        event->call_id.size() > service_mojom::kMaxIdentifierBytes ||
        event->terminal != !event->text.has_value() ||
        (event->complete && !event->terminal) ||
        (event->text &&
         (event->text->empty() ||
          event->text->size() > service_mojom::kMaxTaskAnswerDeltaBytes))) {
      std::move(callback).Run(false);
      return;
    }

    const TaskAnswerKey key{event->task_id, event->call_id};
    auto found = staged_sequences.find(key);
    if (found == staged_sequences.end()) {
      const auto current = task_answer_sequences_.find(key);
      if (current != task_answer_sequences_.end()) {
        found = staged_sequences.emplace(key, current->second).first;
      } else {
        if (event->sequence != 0u ||
            active_calls >= service_mojom::kMaxPendingTaskPolicyPerProfile) {
          std::move(callback).Run(false);
          return;
        }
        found = staged_sequences.emplace(key, TaskAnswerSequenceState{}).first;
        ++active_calls;
      }
    }

    TaskAnswerSequenceState& state = found->second;
    if (state.terminal || event->sequence != state.next_sequence) {
      std::move(callback).Run(false);
      return;
    }
    if (event->terminal) {
      if (active_calls == 0u) {
        std::move(callback).Run(false);
        return;
      }
      state.terminal = true;
      staged_completions.push_back(key);
      --active_calls;
    } else {
      if (state.next_sequence == std::numeric_limits<uint32_t>::max()) {
        std::move(callback).Run(false);
        return;
      }
      ++state.next_sequence;
    }
  }

  for (const auto& [key, state] : staged_sequences) {
    task_answer_sequences_.insert_or_assign(key, state);
  }
  for (auto& key : staged_completions) {
    completed_task_answer_order_.push_back(std::move(key));
  }
  active_task_answer_count_ = active_calls;
  RefreshIdleTeardown();

  // Four complete profile-sized waves are enough to reject any plausible
  // immediate replay without making a transient UI ledger grow for the life
  // of the browser. Live calls are never evicted.
  constexpr size_t kMaxCompletedAnswerTombstones =
      4u * service_mojom::kMaxPendingTaskPolicyPerProfile;
  while (completed_task_answer_order_.size() > kMaxCompletedAnswerTombstones) {
    const TaskAnswerKey oldest =
        std::move(completed_task_answer_order_.front());
    completed_task_answer_order_.pop_front();
    const auto found = task_answer_sequences_.find(oldest);
    if (found != task_answer_sequences_.end() && found->second.terminal) {
      task_answer_sequences_.erase(found);
    }
  }
  for (const auto& event : events) {
    for (Observer& observer : observers_) {
      observer.OnTaskAnswerDelta(event->task_id, event->call_id,
                                 event->sequence, event->text, event->terminal,
                                 event->complete);
    }
  }
  std::move(callback).Run(true);
}

void CoreServiceManager::RevokeRegisteredTerminalTasks() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const TerminalTaskLookup& terminal : state_bindings_.terminal_tasks()) {
    actor_leases_.RevokeTask(terminal.task_id, terminal.service_generation);
    capabilities_.RevokeTask(terminal.task_id, terminal.service_generation);
    // Generation is deliberately absent: BeginGeneration drops the prior
    // generation's vault, so a task identity is already the whole key.
    value_references_.RevokeTask(TaskId{terminal.task_id});
    RevokePreapprovedFormActionsForTask(terminal.task_id);
    accepted_approvals_.RevokeTask(terminal.task_id,
                                   terminal.service_generation);
    tool_artifact_broker_->SettleTask(terminal.task_id,
                                      terminal.service_generation,
                                      TerminalDisposition(terminal.kind));
    effect_broker_->CancelTask(terminal.task_id, terminal.service_generation);
  }
}

void CoreServiceManager::CompleteRegisteredTaskSettlements() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!session_.is_bound() || (availability_ != Availability::kStarting &&
                               availability_ != Availability::kReady)) {
    return;
  }
  for (const TaskSettlementLookup& settlement :
       state_bindings_.task_settlements()) {
    const std::string key = TaskSettlementKey(settlement);
    if (!completed_task_settlements_.insert(key).second) {
      continue;
    }
    actor_leases_.RevokeTask(settlement.task_id, settlement.service_generation);
    capabilities_.RevokeTask(settlement.task_id, settlement.service_generation);
    value_references_.RevokeTask(TaskId{settlement.task_id});
    RevokePreapprovedFormActionsForTask(settlement.task_id);
    if (settlement.kind == service_mojom::TaskSettlementKind::kPause) {
      // Reconcile already moved the exact durable consent into the ledger's
      // inert pause slot. One-use approvals remain revoked, but a later exact,
      // storage-committed Resume may restore the standing consent.
      accepted_approvals_.RevokeTaskApprovals(settlement.task_id,
                                              settlement.service_generation);
    } else {
      accepted_approvals_.RevokeTask(settlement.task_id,
                                     settlement.service_generation);
    }
    const auto disposition =
        settlement.kind == service_mojom::TaskSettlementKind::kPause
            ? ProfileToolArtifactBroker::TaskDisposition::kPaused
            : ProfileToolArtifactBroker::TaskDisposition::kAbandoned;
    tool_artifact_broker_->SettleTask(
        settlement.task_id, settlement.service_generation, disposition);
    auto binding = service_mojom::TaskSettlementBinding::New();
    binding->task_id = settlement.task_id;
    binding->service_generation = settlement.service_generation;
    binding->task_revision = settlement.task_revision;
    binding->kind = settlement.kind;
    effect_broker_->CancelTask(
        settlement.task_id, settlement.service_generation,
        base::BindOnce(&CoreServiceManager::CompleteTaskSettlementAfterEffects,
                       weak_factory_.GetWeakPtr(), std::move(binding)));
  }
}

void CoreServiceManager::CompleteTaskSettlementAfterEffects(
    service_mojom::TaskSettlementBindingPtr binding) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!binding || binding->service_generation != service_generation_ ||
      !session_.is_bound() ||
      (availability_ != Availability::kStarting &&
       availability_ != Availability::kReady)) {
    return;
  }
  session_->CompleteTaskSettlement(std::move(binding));
}

}  // namespace taffy
