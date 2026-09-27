// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <string>
#include <utility>

#include "base/check.h"
#include "mojo/public/cpp/bindings/clone_traits.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_backup.h"
#include "taffy/services/core/service_bridge_backup_ffi.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace core_mojom = core_service::mojom;
namespace internal = core_service_internal;

constexpr uint64_t kGeneration = 17u;

core_mojom::OperationEnvelopePtr Operation(std::string id) {
  return core_mojom::OperationEnvelope::New(std::move(id), kGeneration, 0u,
                                            8'000u, "recovery-resolution-once");
}

core_mojom::BackupRestoreRecoveryBindingPtr Binding() {
  auto output = core_mojom::BackupRestoreRecoveryBinding::New();
  output->reservation_id = "reservation-1";
  output->owner_profile_id = "source-profile";
  output->target_kind = core_mojom::BackupRestoreTargetKind::kNewRegularProfile;
  output->target_profile_id = "target-profile";
  output->backup_id = "backup-1";
  output->snapshot_sha256.assign(32u, 0x11u);
  output->confirmation_sha256.assign(32u, 0x22u);
  output->selection = {core_mojom::BackupRecordKind::kLibraryEntry};
  output->record_count = 1u;
  output->candidate_records_sha256.assign(32u, 0x33u);
  return output;
}

core_mojom::BackupRestoreRecoveryRecordPtr Record(
    uint64_t sequence,
    core_mojom::BackupRestoreRecoveryFactKind kind) {
  auto output = core_mojom::BackupRestoreRecoveryRecord::New();
  output->format_version = 1u;
  output->sequence = sequence;
  output->binding = Binding();
  output->fact_kind = kind;
  if (kind == core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded) {
    output->intent = core_mojom::BackupRestoreRecoveryIntentFact::New(
        "commit-1", core_mojom::BackupRestorePhysicalIntent::kCommitCandidate);
  } else {
    output->outcome = core_mojom::BackupRestoreRecoveryOutcomeFact::New(
        "commit-1", core_mojom::BackupRestoreObservedOutcome::kCompleted);
  }
  return output;
}

core_mojom::BackupRestoreRecoveryResolutionRequestPtr Request() {
  auto output = core_mojom::BackupRestoreRecoveryResolutionRequest::New();
  output->operation = Operation("choose-recovery");
  output->history_prefix.push_back(
      Record(1u, core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded));
  output->history_prefix.push_back(
      Record(2u, core_mojom::BackupRestoreRecoveryFactKind::kOutcomeObserved));
  output->choice = core_mojom::BackupRestoreResolutionChoice::kDiscardCandidate;
  output->intent_id = "discard-1";
  return output;
}

core_mojom::BackupRestoreRecoveryResolutionOutcomeReportPtr Report() {
  auto request = Request();
  auto authorization =
      core_mojom::BackupRestoreRecoveryResolutionAuthorization::New(
          Binding(), request->operation.Clone(), request->choice,
          request->intent_id, mojo::Clone(request->history_prefix));
  auto history = mojo::Clone(request->history_prefix);
  auto intent =
      Record(3u, core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded);
  intent->intent->intent_id = "discard-1";
  intent->intent->intent =
      core_mojom::BackupRestorePhysicalIntent::kDiscardCandidate;
  history.push_back(std::move(intent));
  return core_mojom::BackupRestoreRecoveryResolutionOutcomeReport::New(
      Operation("report-recovery"), std::move(authorization),
      std::move(history));
}

bridge::BridgeBackupRestoreRecoveryResolutionAuthorizationResult BridgeResult(
    bool has_authorization) {
  auto projected =
      internal::ToBridgeBackupRestoreRecoveryResolutionOutcomeReport(*Report());
  auto operation =
      internal::ToBridgeBackupOperation(*Operation("choose-recovery"));
  CHECK(projected);
  CHECK(operation);
  bridge::BridgeBackupRestoreRecoveryResolutionAuthorizationResult output;
  output.operation = std::move(*operation);
  output.status = static_cast<uint8_t>(
      has_authorization
          ? core_mojom::BackupRestoreProtocolStatus::kSucceeded
          : core_mojom::BackupRestoreProtocolStatus::kUnavailable);
  output.has_authorization = has_authorization;
  output.authorization = std::move(projected->authorization);
  return output;
}

TEST(RustCoreBackupRecoveryResolutionTest,
     InputsPreserveExactPrefixBindingChoiceAndDurableSuffix) {
  auto request = Request();
  auto projected =
      internal::ToBridgeBackupRestoreRecoveryResolutionRequest(*request);
  ASSERT_TRUE(projected);
  EXPECT_EQ("choose-recovery", std::string(projected->operation.operation_id));
  EXPECT_EQ(2u, projected->history_prefix.size());
  EXPECT_EQ(static_cast<uint8_t>(request->choice), projected->choice);
  EXPECT_EQ("discard-1", std::string(projected->intent_id));

  auto report = Report();
  auto projected_report =
      internal::ToBridgeBackupRestoreRecoveryResolutionOutcomeReport(*report);
  ASSERT_TRUE(projected_report);
  EXPECT_EQ(3u, projected_report->durable_history.size());
  EXPECT_EQ(
      "reservation-1",
      std::string(projected_report->authorization.binding.reservation_id));
  EXPECT_EQ("discard-1",
            std::string(projected_report->authorization.intent_id));
}

TEST(RustCoreBackupRecoveryResolutionTest,
     InputRejectsUnknownChoiceMissingAuthorityAndOversizedPrefix) {
  auto unknown = Request();
  unknown->choice = static_cast<core_mojom::BackupRestoreResolutionChoice>(255);
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreRecoveryResolutionRequest(*unknown));

  auto report = Report();
  report->authorization.reset();
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreRecoveryResolutionOutcomeReport(*report));

  auto oversized = Request();
  const auto row = oversized->history_prefix.front().Clone();
  while (oversized->history_prefix.size() <=
         core_mojom::kMaxBackupRestoreRecoveryRecords - 3u) {
    oversized->history_prefix.push_back(row.Clone());
  }
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreRecoveryResolutionRequest(*oversized));
}

TEST(RustCoreBackupRecoveryResolutionTest,
     OutputRequiresClosedEnumsAndExactAuthorityPresence) {
  auto successful =
      internal::ToMojoBackupRestoreRecoveryResolutionAuthorizationResult(
          BridgeResult(true));
  ASSERT_TRUE(successful && successful->authorization);
  EXPECT_EQ(core_mojom::BackupRestoreProtocolStatus::kSucceeded,
            successful->status);
  EXPECT_EQ("discard-1", successful->authorization->intent_id);
  EXPECT_EQ(2u, successful->authorization->history_prefix.size());

  auto missing = BridgeResult(true);
  missing.has_authorization = false;
  EXPECT_FALSE(
      internal::ToMojoBackupRestoreRecoveryResolutionAuthorizationResult(
          std::move(missing)));

  auto unknown_status = BridgeResult(true);
  unknown_status.status = 255u;
  EXPECT_FALSE(
      internal::ToMojoBackupRestoreRecoveryResolutionAuthorizationResult(
          std::move(unknown_status)));

  auto unknown_inactive_choice = BridgeResult(false);
  unknown_inactive_choice.authorization.choice = 255u;
  EXPECT_FALSE(
      internal::ToMojoBackupRestoreRecoveryResolutionAuthorizationResult(
          std::move(unknown_inactive_choice)));
}

}  // namespace
}  // namespace taffy
