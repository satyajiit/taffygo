// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>

#include "base/strings/string_util.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_recovery_validation.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

constexpr size_t kRecoveryResolutionSuffixCapacity = 3u;

bool IsKnownChoice(core_mojom::BackupRestoreResolutionChoice choice) {
  switch (choice) {
    case core_mojom::BackupRestoreResolutionChoice::kAcceptCandidate:
    case core_mojom::BackupRestoreResolutionChoice::kDiscardCandidate:
      return true;
  }
  return false;
}

bool IsKnownStatus(core_mojom::BackupRestoreProtocolStatus status) {
  switch (status) {
    case core_mojom::BackupRestoreProtocolStatus::kSucceeded:
    case core_mojom::BackupRestoreProtocolStatus::kInvalidOperation:
    case core_mojom::BackupRestoreProtocolStatus::kUnavailable:
    case core_mojom::BackupRestoreProtocolStatus::kBindingMismatch:
    case core_mojom::BackupRestoreProtocolStatus::kWrongPhase:
    case core_mojom::BackupRestoreProtocolStatus::kConfirmationMismatch:
    case core_mojom::BackupRestoreProtocolStatus::kSnapshotMismatch:
    case core_mojom::BackupRestoreProtocolStatus::kReconcileRequired:
      return true;
  }
  return false;
}

bool IsValidIdentity(std::string_view value) {
  return !value.empty() && value.size() <= core_mojom::kMaxBackupIdBytes &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

bool IsValidHistoryShape(
    const std::vector<core_mojom::BackupRestoreRecoveryRecordPtr>& history,
    size_t maximum,
    std::string_view expected_owner_profile_id) {
  if (history.empty() || history.size() > maximum || !history.front() ||
      !history.front()->binding) {
    return false;
  }
  const auto* binding = history.front()->binding.get();
  return std::ranges::all_of(history, [&](const auto& record) {
    return IsValidBackupRestoreRecoveryRecordShape(record.get(),
                                                   expected_owner_profile_id) &&
           IsExactBackupRestoreRecoveryBinding(binding, record->binding.get());
  });
}

bool IsValidAuthorizationShape(
    const core_mojom::BackupRestoreRecoveryResolutionAuthorization*
        authorization,
    uint64_t service_generation,
    std::string_view expected_owner_profile_id) {
  return authorization && IsKnownChoice(authorization->choice) &&
         IsValidIdentity(authorization->intent_id) &&
         IsLiveBackupOperation(authorization->decision_operation.get(),
                               service_generation, 0u) &&
         IsValidBackupRestoreRecoveryBindingShape(authorization->binding.get(),
                                                  expected_owner_profile_id) &&
         IsValidHistoryShape(authorization->history_prefix,
                             core_mojom::kMaxBackupRestoreRecoveryRecords -
                                 kRecoveryResolutionSuffixCapacity,
                             expected_owner_profile_id) &&
         IsExactBackupRestoreRecoveryBinding(
             authorization->binding.get(),
             authorization->history_prefix.front()->binding.get());
}

bool HasExactPrefix(
    const std::vector<core_mojom::BackupRestoreRecoveryRecordPtr>& prefix,
    const std::vector<core_mojom::BackupRestoreRecoveryRecordPtr>& history) {
  // Both sequences are named by iterator pairs, because the second one is the
  // head of `history` only: the range overload would compare all of it.
  return history.size() >= prefix.size() &&
         std::ranges::equal(
             prefix.begin(), prefix.end(), history.begin(),
             history.begin() + prefix.size(),
             [](const auto& expected, const auto& received) {
               return expected && received && expected->Equals(*received);
             });
}

}  // namespace

bool IsValidBackupRestoreRecoveryResolutionRequest(
    const core_mojom::BackupRestoreRecoveryResolutionRequest& request,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::string_view expected_owner_profile_id) {
  return IsLiveBackupOperation(request.operation.get(), service_generation,
                               now_monotonic_ms) &&
         IsKnownChoice(request.choice) && IsValidIdentity(request.intent_id) &&
         IsValidHistoryShape(request.history_prefix,
                             core_mojom::kMaxBackupRestoreRecoveryRecords -
                                 kRecoveryResolutionSuffixCapacity,
                             expected_owner_profile_id);
}

bool IsValidBackupRestoreRecoveryResolutionAuthorizationResult(
    const core_mojom::BackupRestoreRecoveryResolutionRequest& request,
    const core_mojom::BackupRestoreRecoveryResolutionAuthorizationResult&
        result) {
  if (!IsExactBackupOperation(request.operation.get(),
                              result.operation.get()) ||
      !IsKnownStatus(result.status) ||
      static_cast<bool>(result.authorization) !=
          (result.status ==
           core_mojom::BackupRestoreProtocolStatus::kSucceeded)) {
    return false;
  }
  if (!result.authorization) {
    return true;
  }
  const auto& authorization = result.authorization;
  return IsExactBackupOperation(request.operation.get(),
                                authorization->decision_operation.get()) &&
         authorization->choice == request.choice &&
         authorization->intent_id == request.intent_id &&
         IsExactBackupRestoreRecoveryHistory(request.history_prefix,
                                             authorization->history_prefix) &&
         !request.history_prefix.empty() &&
         IsExactBackupRestoreRecoveryBinding(
             request.history_prefix.front()->binding.get(),
             authorization->binding.get());
}

bool IsValidBackupRestoreRecoveryResolutionOutcomeReport(
    const core_mojom::BackupRestoreRecoveryResolutionOutcomeReport& report,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::string_view expected_owner_profile_id) {
  if (!IsLiveBackupOperation(report.operation.get(), service_generation,
                             now_monotonic_ms) ||
      !IsValidAuthorizationShape(report.authorization.get(), service_generation,
                                 expected_owner_profile_id) ||
      !IsValidHistoryShape(report.durable_history,
                           core_mojom::kMaxBackupRestoreRecoveryRecords,
                           expected_owner_profile_id) ||
      report.durable_history.size() <=
          report.authorization->history_prefix.size() ||
      report.durable_history.size() >
          report.authorization->history_prefix.size() +
              kRecoveryResolutionSuffixCapacity ||
      !HasExactPrefix(report.authorization->history_prefix,
                      report.durable_history)) {
    return false;
  }
  return std::ranges::all_of(report.durable_history, [&](const auto& record) {
    return IsExactBackupRestoreRecoveryBinding(
        report.authorization->binding.get(), record->binding.get());
  });
}

bool IsLiveBackupRestoreRecoveryResolutionAuthorization(
    const core_mojom::BackupRestoreRecoveryResolutionAuthorization*
        authorization,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::string_view expected_owner_profile_id) {
  return IsValidAuthorizationShape(authorization, service_generation,
                                   expected_owner_profile_id) &&
         IsLiveBackupOperation(authorization->decision_operation.get(),
                               service_generation, now_monotonic_ms);
}

}  // namespace taffy
