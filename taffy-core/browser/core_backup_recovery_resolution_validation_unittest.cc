// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <utility>
#include <vector>

#include "mojo/public/cpp/bindings/clone_traits.h"
#include "taffy/browser/core_backup_recovery_validation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kNow = 100u;
constexpr char kOwner[] = "11111111-1111-4111-8111-111111111111";

core_mojom::OperationEnvelopePtr Operation(std::string id,
                                           uint64_t deadline = kNow + 10u) {
  return core_mojom::OperationEnvelope::New(
      std::move(id), kGeneration, 0u, deadline, "recovery-resolution-once");
}

core_mojom::BackupRestoreRecoveryBindingPtr Binding() {
  auto output = core_mojom::BackupRestoreRecoveryBinding::New();
  output->reservation_id = "reservation-1";
  output->owner_profile_id = kOwner;
  output->target_kind = core_mojom::BackupRestoreTargetKind::kNewRegularProfile;
  output->target_profile_id = "22222222-2222-4222-8222-222222222222";
  output->backup_id = "backup-1";
  output->snapshot_sha256.assign(32u, 1u);
  output->confirmation_sha256.assign(32u, 2u);
  output->selection = {core_mojom::BackupRecordKind::kMemoryRecord};
  output->record_count = 1u;
  output->candidate_records_sha256.assign(32u, 3u);
  return output;
}

core_mojom::BackupRestoreRecoveryRecordPtr Intent(
    uint64_t sequence,
    core_mojom::BackupRestorePhysicalIntent intent,
    std::string id) {
  return core_mojom::BackupRestoreRecoveryRecord::New(
      1u, sequence, Binding(),
      core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded,
      core_mojom::BackupRestoreRecoveryIntentFact::New(std::move(id), intent),
      nullptr);
}

core_mojom::BackupRestoreRecoveryRecordPtr Outcome(
    uint64_t sequence,
    core_mojom::BackupRestoreObservedOutcome outcome,
    std::string id) {
  return core_mojom::BackupRestoreRecoveryRecord::New(
      1u, sequence, Binding(),
      core_mojom::BackupRestoreRecoveryFactKind::kOutcomeObserved, nullptr,
      core_mojom::BackupRestoreRecoveryOutcomeFact::New(std::move(id),
                                                        outcome));
}

std::vector<core_mojom::BackupRestoreRecoveryRecordPtr> Prefix() {
  std::vector<core_mojom::BackupRestoreRecoveryRecordPtr> output;
  output.push_back(
      Intent(1u, core_mojom::BackupRestorePhysicalIntent::kCommitCandidate,
             "commit-1"));
  output.push_back(Outcome(
      2u, core_mojom::BackupRestoreObservedOutcome::kCompleted, "commit-1"));
  return output;
}

core_mojom::BackupRestoreRecoveryResolutionRequestPtr Request() {
  return core_mojom::BackupRestoreRecoveryResolutionRequest::New(
      Operation("choose"), Prefix(),
      core_mojom::BackupRestoreResolutionChoice::kDiscardCandidate,
      "discard-1");
}

core_mojom::BackupRestoreRecoveryResolutionAuthorizationPtr Authorization(
    const core_mojom::BackupRestoreRecoveryResolutionRequest& request,
    uint64_t deadline = kNow + 10u) {
  auto decision = request.operation.Clone();
  decision->deadline_monotonic_ms = deadline;
  return core_mojom::BackupRestoreRecoveryResolutionAuthorization::New(
      request.history_prefix.front()->binding.Clone(), std::move(decision),
      request.choice, request.intent_id, mojo::Clone(request.history_prefix));
}

TEST(CoreBackupRecoveryResolutionValidationTest,
     RequestBindsCurrentSourceChoiceIntentAndBoundedPrefix) {
  const auto request = Request();
  EXPECT_TRUE(IsValidBackupRestoreRecoveryResolutionRequest(
      *request, kGeneration, kNow, kOwner));
  for (int mutation = 0; mutation < 9; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = request.Clone();
    switch (mutation) {
      case 0:
        changed->operation->deadline_monotonic_ms = kNow;
        break;
      case 1:
        changed->operation->service_generation++;
        break;
      case 2:
        changed->history_prefix.front()->binding->owner_profile_id = "other";
        break;
      case 3:
        changed->history_prefix.back()->binding->reservation_id = "other";
        break;
      case 4:
        changed->intent_id = "bad\nintent";
        break;
      case 5:
        changed->choice =
            static_cast<core_mojom::BackupRestoreResolutionChoice>(255);
        break;
      case 6:
        changed->history_prefix.clear();
        break;
      case 7:
        changed->history_prefix.front()->binding->record_count =
            core_mojom::kMaxBackupRecords + 1u;
        break;
      case 8:
        changed->history_prefix.front()->intent->intent_id.clear();
        break;
    }
    EXPECT_FALSE(IsValidBackupRestoreRecoveryResolutionRequest(
        *changed, kGeneration, kNow, kOwner));
  }
}

TEST(CoreBackupRecoveryResolutionValidationTest,
     ResultCannotDriftFromTheExactDecisionRequest) {
  const auto request = Request();
  const auto valid =
      core_mojom::BackupRestoreRecoveryResolutionAuthorizationResult::New(
          request->operation.Clone(),
          core_mojom::BackupRestoreProtocolStatus::kSucceeded,
          Authorization(*request));
  EXPECT_TRUE(IsValidBackupRestoreRecoveryResolutionAuthorizationResult(
      *request, *valid));
  for (int mutation = 0; mutation < 6; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = valid.Clone();
    switch (mutation) {
      case 0:
        changed->operation->operation_id += "-other";
        break;
      case 1:
        changed->authorization.reset();
        break;
      case 2:
        changed->authorization->decision_operation->idempotency_key += "-x";
        break;
      case 3:
        changed->authorization->intent_id += "-x";
        break;
      case 4:
        changed->authorization->choice =
            core_mojom::BackupRestoreResolutionChoice::kAcceptCandidate;
        break;
      case 5:
        changed->authorization->history_prefix.back()->sequence++;
        break;
    }
    EXPECT_FALSE(IsValidBackupRestoreRecoveryResolutionAuthorizationResult(
        *request, *changed));
  }
  auto unavailable = valid.Clone();
  unavailable->status = core_mojom::BackupRestoreProtocolStatus::kUnavailable;
  unavailable->authorization.reset();
  EXPECT_TRUE(IsValidBackupRestoreRecoveryResolutionAuthorizationResult(
      *request, *unavailable));
}

TEST(CoreBackupRecoveryResolutionValidationTest,
     ReportUsesFreshRpcWhilePhysicalStartRequiresLiveDecision) {
  const auto request = Request();
  auto authorization = Authorization(*request, kNow - 1u);
  auto history = mojo::Clone(request->history_prefix);
  history.push_back(
      Intent(3u, core_mojom::BackupRestorePhysicalIntent::kDiscardCandidate,
             "discard-1"));
  auto report = core_mojom::BackupRestoreRecoveryResolutionOutcomeReport::New(
      Operation("report"), authorization.Clone(), std::move(history));
  EXPECT_TRUE(IsValidBackupRestoreRecoveryResolutionOutcomeReport(
      *report, kGeneration, kNow, kOwner));
  EXPECT_FALSE(IsLiveBackupRestoreRecoveryResolutionAuthorization(
      authorization.get(), kGeneration, kNow, kOwner));
  authorization->decision_operation->deadline_monotonic_ms = kNow + 1u;
  EXPECT_TRUE(IsLiveBackupRestoreRecoveryResolutionAuthorization(
      authorization.get(), kGeneration, kNow, kOwner));

  auto drifted = report.Clone();
  drifted->durable_history.front()->binding->backup_id = "other";
  EXPECT_FALSE(IsValidBackupRestoreRecoveryResolutionOutcomeReport(
      *drifted, kGeneration, kNow, kOwner));
  drifted = report.Clone();
  drifted->durable_history.erase(drifted->durable_history.begin());
  EXPECT_FALSE(IsValidBackupRestoreRecoveryResolutionOutcomeReport(
      *drifted, kGeneration, kNow, kOwner));
  drifted = report.Clone();
  for (uint64_t sequence = 4u; sequence <= 7u; ++sequence) {
    drifted->durable_history.push_back(Outcome(
        sequence, core_mojom::BackupRestoreObservedOutcome::kOutcomeUnknown,
        "discard-1"));
  }
  EXPECT_FALSE(IsValidBackupRestoreRecoveryResolutionOutcomeReport(
      *drifted, kGeneration, kNow, kOwner));
}

}  // namespace
}  // namespace taffy
