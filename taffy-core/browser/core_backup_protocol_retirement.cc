// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_backup_protocol_validation.h"
#include "taffy/browser/core_service_manager.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

constexpr uint64_t kRetirementCallLifetimeMillis = 30'000u;
constexpr base::TimeDelta kInitialRetryDelay = base::Milliseconds(100);
constexpr base::TimeDelta kMaximumRetryDelay = base::Seconds(5);

bool BoundedText(std::string_view value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

base::TimeDelta RetryDelay(uint32_t failure_count) {
  constexpr uint32_t kMaximumShift = 6u;
  const uint32_t shift = std::min(failure_count, kMaximumShift);
  return std::min(kInitialRetryDelay * (1u << shift), kMaximumRetryDelay);
}

core_mojom::OperationEnvelopePtr NewRetirementOperation(
    uint64_t service_generation,
    std::string_view public_operation_id) {
  const uint64_t now = BackupPlanningNowMonotonicMillis();
  const base::Uuid operation_nonce = base::Uuid::GenerateRandomV4();
  const base::Uuid idempotency_nonce = base::Uuid::GenerateRandomV4();
  if (service_generation == 0u || !operation_nonce.is_valid() ||
      !idempotency_nonce.is_valid() ||
      !BoundedText(public_operation_id, core_mojom::kMaxOperationIdBytes) ||
      now > std::numeric_limits<uint64_t>::max() -
                kRetirementCallLifetimeMillis) {
    return nullptr;
  }
  auto operation = core_mojom::OperationEnvelope::New(
      "backup-restore-retire-" + operation_nonce.AsLowercaseString(),
      service_generation, 0u, now + kRetirementCallLifetimeMillis,
      "backup-restore-retire-" + idempotency_nonce.AsLowercaseString() + "-" +
          std::string(public_operation_id));
  return IsLiveBackupOperation(operation.get(), service_generation, now)
             ? std::move(operation)
             : nullptr;
}

}  // namespace

// Process-local custody for withdrawing one source-Core-owned precommit plan.
// It remembers no physical target handle and can only issue exact cancellation
// requests. Its workflow interest prevents idle teardown from silently losing
// the retained plan between retries.
class CoreBackupProtocolRetirementState final {
 public:
  explicit CoreBackupProtocolRetirementState(CoreBackupProtocol* protocol)
      : protocol_(protocol) {
    CHECK(protocol_);
  }

  CoreBackupProtocolRetirementState(const CoreBackupProtocolRetirementState&) =
      delete;
  CoreBackupProtocolRetirementState& operator=(
      const CoreBackupProtocolRetirementState&) = delete;
  ~CoreBackupProtocolRetirementState() = default;

  bool Retain(std::string public_operation_id,
              core_mojom::BackupRestoreBindingPtr binding,
              CoreBackupProtocol::WorkflowInterest interest) {
    if (retained_) {
      return IsExactBackupRestoreBinding(retained_->binding.get(),
                                         binding.get());
    }
    retained_ =
        std::make_unique<Retained>(std::move(public_operation_id),
                                   std::move(binding), std::move(interest));
    Attempt();
    return true;
  }

  bool BlocksPlanning() const { return retained_ != nullptr; }

  bool NamesRetainedBinding(
      const core_mojom::BackupRestoreBinding* binding) const {
    return retained_ &&
           IsExactBackupRestoreBinding(retained_->binding.get(), binding);
  }

  base::WeakPtr<CoreBackupProtocolRetirementState> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  void CancellationAdmitted(const core_mojom::OperationEnvelope& operation,
                            const core_mojom::BackupRestoreBinding& binding) {
    pending_cancellations_.insert_or_assign(
        operation.operation_id,
        PendingCancellation{.binding = binding.Clone()});
    if (NamesRetainedBinding(&binding)) {
      retained_->retry_timer.Stop();
    }
  }

  void CancellationCompleted(const std::string& operation_id,
                             const core_mojom::BackupRestoreBinding& binding,
                             core_mojom::BackupRestoreProtocolStatus status) {
    pending_cancellations_.erase(operation_id);
    if (!NamesRetainedBinding(&binding)) {
      return;
    }
    if (status == core_mojom::BackupRestoreProtocolStatus::kSucceeded) {
      retained_.reset();
      return;
    }
    ScheduleRetry();
  }

  void CancellationRejected(const core_mojom::BackupRestoreBinding* binding) {
    if (NamesRetainedBinding(binding)) {
      ScheduleRetry();
    }
  }

  void SourceDisconnected() {
    pending_cancellations_.clear();
    retained_.reset();
  }

 private:
  struct PendingCancellation {
    core_mojom::BackupRestoreBindingPtr binding;
  };

  struct Retained {
    Retained(std::string operation_id,
             core_mojom::BackupRestoreBindingPtr retained_binding,
             CoreBackupProtocol::WorkflowInterest workflow_interest)
        : public_operation_id(std::move(operation_id)),
          binding(std::move(retained_binding)),
          interest(std::move(workflow_interest)) {}

    std::string public_operation_id;
    core_mojom::BackupRestoreBindingPtr binding;
    CoreBackupProtocol::WorkflowInterest interest;
    base::OneShotTimer retry_timer;
    uint32_t failure_count = 0u;
  };

  bool HasPendingCancellation() const {
    return retained_ &&
           std::ranges::any_of(
               pending_cancellations_, [this](const auto& entry) {
                 return IsExactBackupRestoreBinding(retained_->binding.get(),
                                                    entry.second.binding.get());
               });
  }

  void Arm(base::TimeDelta delay) {
    if (!retained_) {
      return;
    }
    retained_->retry_timer.Start(
        FROM_HERE, std::max(delay, base::Milliseconds(1)),
        base::BindOnce(&CoreBackupProtocolRetirementState::Attempt,
                       weak_factory_.GetWeakPtr()));
  }

  void ScheduleRetry() {
    if (!retained_) {
      return;
    }
    if (HasPendingCancellation()) {
      retained_->retry_timer.Stop();
      return;
    }
    const base::TimeDelta delay = RetryDelay(retained_->failure_count);
    if (retained_->failure_count != std::numeric_limits<uint32_t>::max()) {
      ++retained_->failure_count;
    }
    Arm(delay);
  }

  void Attempt() {
    if (!retained_) {
      return;
    }
    if (HasPendingCancellation()) {
      return;
    }
    const std::string public_operation_id = retained_->public_operation_id;
    core_mojom::BackupRestoreBindingPtr binding = retained_->binding.Clone();
    protocol_->DispatchBackupRestoreRetirementAttempt(public_operation_id,
                                                      *binding);
  }

  const raw_ptr<CoreBackupProtocol> protocol_;
  base::flat_map<std::string, PendingCancellation> pending_cancellations_;
  std::unique_ptr<Retained> retained_;
  base::WeakPtrFactory<CoreBackupProtocolRetirementState> weak_factory_{this};
};

CoreBackupProtocol::CoreBackupProtocol(CoreServiceManager* manager)
    : manager_(manager),
      retirement_(std::make_unique<CoreBackupProtocolRetirementState>(this)) {
  CHECK(manager_);
}

CoreBackupProtocol::~CoreBackupProtocol() {
  retirement_->SourceDisconnected();
}

bool CoreBackupProtocol::RetireBackupRestorePlan(
    std::string public_operation_id,
    core_mojom::BackupRestoreBindingPtr binding) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  if (!binding ||
      !BoundedText(public_operation_id, core_mojom::kMaxOperationIdBytes) ||
      manager_->shutdown_started_ ||
      manager_->availability_ != CoreServiceAvailability::kReady ||
      !manager_->session_.is_bound() ||
      binding->owner_profile_id != manager_->browser_profile_id_ ||
      !IsValidBackupRestoreBinding(binding.get(),
                                   manager_->service_generation_)) {
    return false;
  }
  if (retirement_->BlocksPlanning()) {
    return retirement_->NamesRetainedBinding(binding.get());
  }
  auto interest = AcquireWorkflowInterest(
      base::BindOnce(&CoreBackupProtocolRetirementState::SourceDisconnected,
                     retirement_->GetWeakPtr()));
  return interest &&
         retirement_->Retain(std::move(public_operation_id), std::move(binding),
                             std::move(*interest));
}

bool CoreBackupProtocol::BackupRestorePlanningBlockedByRetirement() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  return retirement_->BlocksPlanning();
}

void CoreBackupProtocol::ObserveBackupRestoreCancellationAdmitted(
    const core_mojom::OperationEnvelope& operation,
    const core_mojom::BackupRestoreBinding& binding) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  retirement_->CancellationAdmitted(operation, binding);
}

void CoreBackupProtocol::ObserveBackupRestoreCancellationCompleted(
    const std::string& operation_id,
    const core_mojom::BackupRestoreBinding& binding,
    core_mojom::BackupRestoreProtocolStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  retirement_->CancellationCompleted(operation_id, binding, status);
}

void CoreBackupProtocol::ObserveBackupRestoreCancellationRejected(
    const core_mojom::BackupRestoreBinding* binding) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  retirement_->CancellationRejected(binding);
}

void CoreBackupProtocol::OnBackupRestoreRetirementSourceDisconnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  retirement_->SourceDisconnected();
}

void CoreBackupProtocol::DispatchBackupRestoreRetirementAttempt(
    const std::string& public_operation_id,
    const core_mojom::BackupRestoreBinding& binding) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager_->sequence_checker_);
  core_mojom::OperationEnvelopePtr operation = NewRetirementOperation(
      manager_->service_generation_, public_operation_id);
  if (!operation) {
    ObserveBackupRestoreCancellationRejected(&binding);
    return;
  }
  auto request = core_mojom::BackupRestoreCancellationRequest::New(
      std::move(operation), binding.Clone());
  CancelBackupRestoreBeforeCommit(
      std::move(request),
      base::BindOnce([](core_mojom::BackupRestoreProtocolResultPtr) {}));
}

}  // namespace taffy
