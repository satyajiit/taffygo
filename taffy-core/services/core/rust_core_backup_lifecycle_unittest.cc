// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
constexpr uint64_t kGeneration = 9u;
constexpr uint64_t kNow = 100u;
constexpr char kSource[] = "11111111-1111-4111-8111-111111111111";
constexpr char kTarget[] = "22222222-2222-4222-8222-222222222222";

mojom::OperationEnvelopePtr Operation(const char* id, uint64_t now = kNow) {
  return mojom::OperationEnvelope::New(id, kGeneration, 0u, now + 10u,
                                       std::string(id) + "-once");
}

class RustCoreBackupLifecycleTest : public testing::Test {
 protected:
  void SetUp() override {
    auto bootstrap = mojom::CoreBootstrap::New();
    bootstrap->service_generation = kGeneration;
    bootstrap->browser_profile_id = kSource;
    bootstrap->browser_session_id = "browser-session";
    for (uint8_t value = 1u; value <= 32u; ++value) {
      bootstrap->generation_capability_entropy.push_back(value);
    }
    bootstrap->available_account_methods = {
        mojom::AccountAuthMethod::kGoogle, mojom::AccountAuthMethod::kEmailLink,
        mojom::AccountAuthMethod::kGithub, mojom::AccountAuthMethod::kFacebook};
    auto initialized = core_.Initialize(std::move(bootstrap));
    ASSERT_TRUE(initialized.result);
    ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);

    auto request = mojom::BackupManifestPrepareRequest::New();
    request->operation = Operation("prepare");
    request->backup_id = "backup-1";
    request->source_installation_id = "installation-1";
    request->created_at_utc = "2026-09-05T00:00:00Z";
    request->selection = {mojom::BackupRecordKind::kLibraryEntry};
    auto record = mojom::BackupRecordDescriptor::New();
    record->kind = mojom::BackupRecordKind::kLibraryEntry;
    record->stable_id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    record->revision = 1u;
    record->schema_version = 1u;
    record->state = mojom::BackupRecordState::kActive;
    record->plaintext_bytes = 4u;
    record->plaintext_sha256.assign(32u, 1u);
    request->records.push_back(std::move(record));
    auto prepared =
        core_.PrepareBackupManifest(std::move(request), kGeneration, kNow);
    ASSERT_TRUE(prepared);
    ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, prepared->status);
    manifest_ = std::move(prepared->manifest_plaintext);
  }

  mojom::BackupRestorePlanResultPtr Plan(const char* operation_id,
                                         uint64_t now = kNow) {
    auto request = mojom::BackupRestorePlanRequest::New();
    request->operation = Operation(operation_id, now);
    request->manifest_plaintext = manifest_;
    request->staged_records.push_back(
        mojom::StagedBackupRecord::New(4u, std::vector<uint8_t>(32u, 1u)));
    request->target = mojom::BackupRestoreTarget::New(
        mojom::BackupRestoreTargetKind::kNewRegularProfile, kTarget);
    return core_.PlanBackupRestore(std::move(request), kGeneration, now);
  }

  void ExerciseResolution(mojom::BackupRestoreResolutionChoice choice) {
    // Real Mojo/CXX/Rust calls and one owned portable session. Physical
    // verification/outcome reports are simulated, not a database commit drill.
    auto plan = Plan("plan");
    ASSERT_TRUE(plan);
    ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, plan->status);
    ASSERT_TRUE(plan->binding);
    EXPECT_EQ(kSource, plan->binding->owner_profile_id);
    ASSERT_TRUE(plan->binding->target);
    EXPECT_EQ(kTarget, plan->binding->target->profile_id);

    auto confirmed = core_.ConfirmBackupRestorePlan(
        mojom::BackupRestorePlanConfirmationRequest::New(
            Operation("confirm", 500u), plan->binding.Clone(),
            plan->confirmation_sha256),
        500u);
    ASSERT_TRUE(confirmed);
    ASSERT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded,
              confirmed->status);
    ASSERT_TRUE(confirmed->authorization);
    auto verified = core_.ReportBackupRestoreStageVerified(
        mojom::BackupRestoreStageVerificationRequest::New(
            Operation("verify", 1'000u), confirmed->authorization.Clone(),
            plan->snapshot_sha256, std::vector<mojom::SkillRecordPtr>{}),
        1'000u);
    ASSERT_TRUE(verified);
    ASSERT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded, verified->status);
    ASSERT_TRUE(verified->authorization);
    auto committed = core_.ReportBackupRestoreCommitOutcome(
        mojom::BackupRestoreCommitOutcomeReport::New(
            Operation("committed", 2'000u), verified->authorization.Clone(),
            mojom::BackupRestoreCommitOutcome::kCommitted),
        2'000u);
    ASSERT_TRUE(committed);
    ASSERT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded,
              committed->status);
    auto resolution = core_.ChooseBackupRestoreResolution(
        mojom::BackupRestoreResolutionRequest::New(
            Operation("resolve", 3'000u), plan->binding.Clone(), choice),
        3'000u);
    ASSERT_TRUE(resolution);
    ASSERT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded,
              resolution->status);
    ASSERT_TRUE(resolution->authorization);
    EXPECT_EQ(choice, resolution->authorization->choice);
    auto resolved = core_.ReportBackupRestoreResolutionOutcome(
        mojom::BackupRestoreResolutionOutcomeReport::New(
            Operation("resolved", 4'000u), resolution->authorization.Clone(),
            mojom::BackupRestoreResolutionOutcome::kCompleted),
        4'000u);
    ASSERT_TRUE(resolved);
    EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded, resolved->status);

    // Completion consumes this session. It cannot authorize a repeated choice,
    // and the same source can admit a subsequent distinct restore operation.
    auto repeated = core_.ChooseBackupRestoreResolution(
        mojom::BackupRestoreResolutionRequest::New(
            Operation("repeat", 5'000u), plan->binding.Clone(), choice),
        5'000u);
    ASSERT_TRUE(repeated);
    EXPECT_NE(mojom::BackupRestoreProtocolStatus::kSucceeded, repeated->status);
    EXPECT_FALSE(repeated->authorization);
    auto next = Plan("next", 6'000u);
    ASSERT_TRUE(next);
    EXPECT_EQ(mojom::BackupPlanningStatus::kSucceeded, next->status);
  }

  RustCore core_;
  std::vector<uint8_t> manifest_;
};

TEST_F(RustCoreBackupLifecycleTest, AcceptanceCrossesTheOwnedShippingBridge) {
  ExerciseResolution(mojom::BackupRestoreResolutionChoice::kAcceptCandidate);
}

TEST_F(RustCoreBackupLifecycleTest, DiscardCrossesTheOwnedShippingBridge) {
  ExerciseResolution(mojom::BackupRestoreResolutionChoice::kDiscardCandidate);
}

TEST_F(RustCoreBackupLifecycleTest, CancellationRetiresOnlyTheExactPlan) {
  auto plan = Plan("plan");
  ASSERT_TRUE(plan);
  ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, plan->status);
  ASSERT_TRUE(plan->binding);
  auto changed = plan->binding.Clone();
  changed->backup_id = "another-backup";
  auto refused = core_.CancelBackupRestoreBeforeCommit(
      mojom::BackupRestoreCancellationRequest::New(
          Operation("wrong-cancel", 500u), std::move(changed)),
      500u);
  ASSERT_TRUE(refused);
  EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kBindingMismatch,
            refused->status);
  auto cancelled = core_.CancelBackupRestoreBeforeCommit(
      mojom::BackupRestoreCancellationRequest::New(Operation("cancel", 600u),
                                                   plan->binding.Clone()),
      600u);
  ASSERT_TRUE(cancelled);
  EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded, cancelled->status);
  auto next = Plan("next", 700u);
  ASSERT_TRUE(next);
  EXPECT_EQ(mojom::BackupPlanningStatus::kSucceeded, next->status);
}

TEST_F(RustCoreBackupLifecycleTest,
       CancelledBindingReceiptCannotRetireTheNextPlan) {
  auto plan = Plan("plan");
  ASSERT_TRUE(plan);
  ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, plan->status);
  ASSERT_TRUE(plan->binding);
  auto cancelled = core_.CancelBackupRestoreBeforeCommit(
      mojom::BackupRestoreCancellationRequest::New(Operation("cancel", 500u),
                                                   plan->binding.Clone()),
      500u);
  ASSERT_TRUE(cancelled);
  ASSERT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded, cancelled->status);
  auto replay = Plan("plan");
  ASSERT_TRUE(replay);
  EXPECT_EQ(mojom::BackupPlanningStatus::kInvalidRequest, replay->status);
  EXPECT_FALSE(replay->binding);
  auto retry = core_.CancelBackupRestoreBeforeCommit(
      mojom::BackupRestoreCancellationRequest::New(Operation("retry", 600u),
                                                   plan->binding.Clone()),
      600u);
  ASSERT_TRUE(retry);
  EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded, retry->status);
  auto next = Plan("next", 700u);
  ASSERT_TRUE(next);
  ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, next->status);
  auto old_retry = core_.CancelBackupRestoreBeforeCommit(
      mojom::BackupRestoreCancellationRequest::New(Operation("old-retry", 800u),
                                                   plan->binding.Clone()),
      800u);
  ASSERT_TRUE(old_retry);
  EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded, old_retry->status);
  auto next_confirmation = core_.ConfirmBackupRestorePlan(
      mojom::BackupRestorePlanConfirmationRequest::New(
          Operation("next-confirm", 900u), next->binding.Clone(),
          next->confirmation_sha256),
      900u);
  ASSERT_TRUE(next_confirmation);
  EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kSucceeded,
            next_confirmation->status);
  EXPECT_TRUE(next_confirmation->authorization);
}

TEST_F(RustCoreBackupLifecycleTest, ShutdownWithdrawsRetainedPlanAuthority) {
  auto plan = Plan("plan");
  ASSERT_TRUE(plan);
  ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, plan->status);
  ASSERT_TRUE(plan->binding);
  core_.PrepareForShutdown();
  auto refused = core_.ConfirmBackupRestorePlan(
      mojom::BackupRestorePlanConfirmationRequest::New(
          Operation("confirm", 500u), plan->binding.Clone(),
          plan->confirmation_sha256),
      500u);
  // Shutdown destroys RustCore's bridge. Its internal API yields no result;
  // CoreServiceImpl's separately tested reply boundary turns that absence
  // into a non-null unavailable record with no authority for Mojo callers.
  EXPECT_FALSE(refused);
}

TEST_F(RustCoreBackupLifecycleTest,
       ExtraStagedProcedureReachesRustAndRefusesCommit) {
  auto plan = Plan("plan");
  ASSERT_TRUE(plan);
  ASSERT_TRUE(plan->binding);
  auto confirmed = core_.ConfirmBackupRestorePlan(
      mojom::BackupRestorePlanConfirmationRequest::New(
          Operation("confirm"), plan->binding.Clone(),
          plan->confirmation_sha256),
      kNow);
  ASSERT_TRUE(confirmed);
  ASSERT_TRUE(confirmed->authorization);
  std::vector<mojom::SkillRecordPtr> skills;
  skills.push_back(mojom::SkillRecord::New(
      "not-selected", "https://example.test", mojom::SkillProvenance::kAuthored,
      mojom::SkillStatus::kDraft, 1u, std::vector<uint8_t>{1u}, 1u, 1u, 2u));
  auto result = core_.ReportBackupRestoreStageVerified(
      mojom::BackupRestoreStageVerificationRequest::New(
          Operation("verify"), confirmed->authorization.Clone(),
          plan->snapshot_sha256, std::move(skills)),
      kNow);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status,
            mojom::BackupRestoreProtocolStatus::kSnapshotMismatch);
  EXPECT_FALSE(result->authorization);
  // If any Mojo/CXX/bridge projection silently discarded the new field,
  // this library-only plan would have received commit authority above.
}

}  // namespace
}  // namespace taffy
