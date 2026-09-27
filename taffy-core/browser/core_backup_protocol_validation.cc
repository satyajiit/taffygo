// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_backup_protocol_validation.h"

#include <algorithm>
#include <limits>

#include "base/strings/string_util.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsBoundedText(std::string_view value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

template <typename Range>
bool IsNonZeroDigest(const Range& digest) {
  return digest.size() == 32u &&
         std::ranges::any_of(digest, [](uint8_t byte) { return byte != 0u; });
}

bool IsKnownStatus(mojom::BackupRestoreProtocolStatus status) {
  switch (status) {
    case mojom::BackupRestoreProtocolStatus::kSucceeded:
    case mojom::BackupRestoreProtocolStatus::kInvalidOperation:
    case mojom::BackupRestoreProtocolStatus::kUnavailable:
    case mojom::BackupRestoreProtocolStatus::kBindingMismatch:
    case mojom::BackupRestoreProtocolStatus::kWrongPhase:
    case mojom::BackupRestoreProtocolStatus::kConfirmationMismatch:
    case mojom::BackupRestoreProtocolStatus::kSnapshotMismatch:
    case mojom::BackupRestoreProtocolStatus::kReconcileRequired:
      return true;
  }
  return false;
}

bool AreStagedSkillsBounded(const std::vector<mojom::SkillRecordPtr>& skills) {
  if (skills.size() > mojom::kMaxSkillsPerProfile) {
    return false;
  }
  for (const auto& skill : skills) {
    if (!skill || !IsBoundedText(skill->skill_id, mojom::kMaxSkillIdBytes) ||
        !IsBoundedText(skill->origin, mojom::kMaxNormalizedOriginBytes) ||
        !core_service::wire::SkillProvenanceFromWire(
            static_cast<uint32_t>(skill->provenance)) ||
        !core_service::wire::SkillStatusFromWire(
            static_cast<uint32_t>(skill->status)) ||
        skill->active_version == 0u ||
        skill->active_version > mojom::kMaxSkillVersionsPerSkill ||
        skill->definition.empty() ||
        skill->definition.size() > mojom::kMaxSkillDefinitionBytes ||
        skill->step_count == 0u || skill->step_count > mojom::kMaxSkillSteps ||
        skill->installed_at_utc_ms >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        skill->updated_at_utc_ms >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
  }
  return true;
}

bool IsKnownCommitOutcome(mojom::BackupRestoreCommitOutcome outcome) {
  switch (outcome) {
    case mojom::BackupRestoreCommitOutcome::kCommitted:
    case mojom::BackupRestoreCommitOutcome::kDefinitelyNotCommitted:
    case mojom::BackupRestoreCommitOutcome::kOutcomeUnknown:
      return true;
  }
  return false;
}

bool IsKnownChoice(mojom::BackupRestoreResolutionChoice choice) {
  switch (choice) {
    case mojom::BackupRestoreResolutionChoice::kAcceptCandidate:
    case mojom::BackupRestoreResolutionChoice::kDiscardCandidate:
      return true;
  }
  return false;
}

bool IsKnownResolutionOutcome(mojom::BackupRestoreResolutionOutcome outcome) {
  switch (outcome) {
    case mojom::BackupRestoreResolutionOutcome::kCompleted:
    case mojom::BackupRestoreResolutionOutcome::kDefinitelyNotCompleted:
    case mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown:
      return true;
  }
  return false;
}

template <typename Authorization>
bool IsValidAuthorization(const Authorization* authorization,
                          uint64_t service_generation) {
  return authorization &&
         IsValidBackupRestoreBinding(authorization->binding.get(),
                                     service_generation) &&
         IsLiveBackupOperation(authorization->decision_operation.get(),
                               service_generation, 0u);
}

template <typename Result>
bool HasExpectedResultShape(const mojom::OperationEnvelope& expected_operation,
                            const Result& result) {
  if (!IsExactBackupOperation(&expected_operation, result.operation.get()) ||
      !IsKnownStatus(result.status)) {
    return false;
  }
  return (result.status == mojom::BackupRestoreProtocolStatus::kSucceeded) ==
         static_cast<bool>(result.authorization);
}

}  // namespace

bool IsValidBackupRestoreBinding(const mojom::BackupRestoreBinding* binding,
                                 uint64_t service_generation) {
  return binding &&
         IsLiveBackupOperation(binding->planning_operation.get(),
                               service_generation, 0u) &&
         IsBoundedText(binding->owner_profile_id, mojom::kMaxBackupIdBytes) &&
         binding->target &&
         binding->target->kind ==
             mojom::BackupRestoreTargetKind::kNewRegularProfile &&
         IsBoundedText(binding->target->profile_id, mojom::kMaxBackupIdBytes) &&
         binding->owner_profile_id != binding->target->profile_id &&
         IsBoundedText(binding->backup_id, mojom::kMaxBackupIdBytes) &&
         IsNonZeroDigest(binding->snapshot_sha256) &&
         IsNonZeroDigest(binding->confirmation_sha256);
}

bool IsExactBackupRestoreBinding(const mojom::BackupRestoreBinding* expected,
                                 const mojom::BackupRestoreBinding* received) {
  return expected && received &&
         IsExactBackupOperation(expected->planning_operation.get(),
                                received->planning_operation.get()) &&
         expected->owner_profile_id == received->owner_profile_id &&
         expected->target && received->target &&
         expected->target->kind == received->target->kind &&
         expected->target->profile_id == received->target->profile_id &&
         expected->backup_id == received->backup_id &&
         expected->snapshot_sha256 == received->snapshot_sha256 &&
         expected->confirmation_sha256 == received->confirmation_sha256;
}

bool IsValidBackupRestorePlanConfirmationRequest(
    const mojom::BackupRestorePlanConfirmationRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         IsValidBackupRestoreBinding(request.binding.get(),
                                     service_generation) &&
         IsNonZeroDigest(request.confirmed_sha256);
}

bool IsValidBackupRestoreStageVerificationRequest(
    const mojom::BackupRestoreStageVerificationRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         IsValidAuthorization(request.authorization.get(),
                              service_generation) &&
         IsNonZeroDigest(request.staged_snapshot_sha256) &&
         AreStagedSkillsBounded(request.skills);
}

bool IsValidBackupRestoreCommitOutcomeReport(
    const mojom::BackupRestoreCommitOutcomeReport& report,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return IsLiveBackupOperation(report.operation.get(), service_generation,
                               now_monotonic_ms) &&
         IsValidAuthorization(report.authorization.get(), service_generation) &&
         IsKnownCommitOutcome(report.outcome);
}

bool IsValidBackupRestoreResolutionRequest(
    const mojom::BackupRestoreResolutionRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         IsValidBackupRestoreBinding(request.binding.get(),
                                     service_generation) &&
         IsKnownChoice(request.choice);
}

bool IsValidBackupRestoreResolutionOutcomeReport(
    const mojom::BackupRestoreResolutionOutcomeReport& report,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return IsLiveBackupOperation(report.operation.get(), service_generation,
                               now_monotonic_ms) &&
         IsValidAuthorization(report.authorization.get(), service_generation) &&
         IsKnownChoice(report.authorization->choice) &&
         IsKnownResolutionOutcome(report.outcome);
}

bool IsValidBackupRestoreCancellationRequest(
    const mojom::BackupRestoreCancellationRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         IsValidBackupRestoreBinding(request.binding.get(), service_generation);
}

bool IsValidBackupRestoreStageAuthorizationResult(
    const mojom::OperationEnvelope& expected_operation,
    const mojom::BackupRestoreBinding& expected_binding,
    const mojom::BackupRestoreStageAuthorizationResult& result) {
  return HasExpectedResultShape(expected_operation, result) &&
         (!result.authorization ||
          (IsExactBackupRestoreBinding(&expected_binding,
                                       result.authorization->binding.get()) &&
           IsExactBackupOperation(
               &expected_operation,
               result.authorization->decision_operation.get())));
}

bool IsValidBackupRestoreCommitAuthorizationResult(
    const mojom::OperationEnvelope& expected_operation,
    const mojom::BackupRestoreBinding& expected_binding,
    const mojom::BackupRestoreCommitAuthorizationResult& result) {
  return HasExpectedResultShape(expected_operation, result) &&
         (!result.authorization ||
          (IsExactBackupRestoreBinding(&expected_binding,
                                       result.authorization->binding.get()) &&
           IsExactBackupOperation(
               &expected_operation,
               result.authorization->decision_operation.get())));
}

bool IsValidBackupRestoreResolutionAuthorizationResult(
    const mojom::OperationEnvelope& expected_operation,
    const mojom::BackupRestoreBinding& expected_binding,
    mojom::BackupRestoreResolutionChoice expected_choice,
    const mojom::BackupRestoreResolutionAuthorizationResult& result) {
  return IsKnownChoice(expected_choice) &&
         HasExpectedResultShape(expected_operation, result) &&
         (!result.authorization ||
          (IsExactBackupRestoreBinding(&expected_binding,
                                       result.authorization->binding.get()) &&
           IsExactBackupOperation(
               &expected_operation,
               result.authorization->decision_operation.get()) &&
           result.authorization->choice == expected_choice));
}

bool IsValidBackupRestoreProtocolResult(
    const mojom::OperationEnvelope& expected_operation,
    const mojom::BackupRestoreProtocolResult& result) {
  return IsExactBackupOperation(&expected_operation, result.operation.get()) &&
         IsKnownStatus(result.status);
}

bool IsLiveBackupRestoreCommitAuthorization(
    const mojom::BackupRestoreCommitAuthorization* authorization,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return authorization &&
         IsValidBackupRestoreBinding(authorization->binding.get(),
                                     service_generation) &&
         IsLiveBackupOperation(authorization->decision_operation.get(),
                               service_generation, now_monotonic_ms);
}

bool IsLiveBackupRestoreResolutionAuthorization(
    const mojom::BackupRestoreResolutionAuthorization* authorization,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  return authorization && IsKnownChoice(authorization->choice) &&
         IsValidBackupRestoreBinding(authorization->binding.get(),
                                     service_generation) &&
         IsLiveBackupOperation(authorization->decision_operation.get(),
                               service_generation, now_monotonic_ms);
}

}  // namespace taffy
