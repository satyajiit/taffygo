// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol_validation.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_test_support.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr char kSourceProfile[] = "11111111-1111-4111-8111-111111111111";
constexpr char kTargetProfile[] = "22222222-2222-4222-8222-222222222222";
constexpr char kInstallation[] = "33333333-3333-4333-8333-333333333333";

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::BackupRestorePlanResultPtr RestorePlan(uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New("restore-plan", generation, 0u,
                                                 NowMonotonicMillis() + 30'000u,
                                                 "restore-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, kTargetProfile);
  auto result = mojom::BackupRestorePlanResult::New();
  result->operation = operation.Clone();
  result->status = mojom::BackupPlanningStatus::kSucceeded;
  result->backup_id = "backup-1";
  result->snapshot_sha256.assign(32u, 2u);
  result->target = target.Clone();
  result->confirmation_sha256.assign(32u, 3u);
  result->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), kSourceProfile, std::move(target),
      result->backup_id, result->snapshot_sha256, result->confirmation_sha256);
  return result;
}

std::unique_ptr<CoreServiceManager> MakeManager(
    test::QuietManagerTail& tail,
    content::TestBrowserContext* context) {
  auto tools = std::make_unique<ProfileToolSupervisor>(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  return tail.MakeManager(
      context, /*storage_broker=*/nullptr, std::move(tools),
      base::MakeRefCounted<CorePageObservationBroker>(context),
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
}

class ProfileBackupCoordinatorStagingTest : public testing::Test {
 protected:
  void SetUp() override {
    context_ = std::make_unique<content::TestBrowserContext>();
    manager_ = MakeManager(tail_, context_.get());
    service_ = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_,
                                                              kSourceProfile);
    ASSERT_TRUE(profile_.CreateUniqueTempDir());
    stage_store_ = storage::backup::BackupArchiveStageStore::Create(
        profile_.GetPath().Append(
            storage::backup::kBackupStagingDirectoryName));
    ASSERT_TRUE(stage_store_);
    coordinator_ = std::make_unique<ProfileBackupCoordinator>(
        manager_.get(), stage_store_, kInstallation);
  }

  void TearDown() override {
    coordinator_.reset();
    manager_->Shutdown();
    task_environment_.RunUntilIdle();
  }

  base::File Payload() {
    const std::array<uint8_t, 3> bytes = {1u, 2u, 3u};
    base::FilePath path;
    base::File file =
        base::CreateAndOpenTemporaryFileInDir(profile_.GetPath(), &path);
    if (!file.IsValid() || !file.WriteAtCurrentPosAndCheck(bytes) ||
        !file.Flush()) {
      return base::File();
    }
    return file;
  }

  void Ready(std::string operation_id) {
    storage::backup::Secret key{};
    key.fill(1u);
    ASSERT_TRUE(coordinator_->BeginImport(operation_id, key).has_value());
    ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::SetReadyRestore(
        *coordinator_, operation_id, target_.TakeOwner(),
        RestorePlan(manager_->service_generation()), Payload()));
  }

  void SettleCancellation(const std::string& operation_id) {
    coordinator_->Cancel(operation_id);
    if (ProfileBackupCoordinatorTestPeer::IsPortableCancellationInFlight(
            *coordinator_, operation_id)) {
      ProfileBackupCoordinatorTestPeer::DeliverCancellation(
          *coordinator_, operation_id,
          mojom::BackupRestoreProtocolStatus::kSucceeded);
    }
    if (target_.cleanup_pending()) {
      target_.CompleteCleanup(true);
    }
  }

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<content::TestBrowserContext> context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::TaffyCoreService> service_;
  mojo::PendingReceiver<mojom::CoreSession> session_;
  base::ScopedTempDir profile_;
  scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store_;
  ProfileBackupRestoreTargetTestState target_{kTargetProfile};
  std::unique_ptr<ProfileBackupCoordinator> coordinator_;
};

TEST_F(ProfileBackupCoordinatorStagingTest,
       WrongDigestNeverRequestsPhysicalStaging) {
  constexpr char kOperation[] = "wrong-digest";
  Ready(kOperation);
  int callbacks = 0;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 4u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++callbacks;
            ASSERT_FALSE(result);
            EXPECT_EQ(ProfileBackupError::kPlanRefused, result.error());
          }));

  EXPECT_EQ(1, callbacks);
  EXPECT_EQ(0, target_.stage_calls());
  EXPECT_FALSE(ProfileBackupCoordinatorTestPeer::IsConfirming(*coordinator_,
                                                              kOperation));
  SettleCancellation(kOperation);
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorStagingTest,
       ExactAuthorizationStagesOnceAndEchoesAuthority) {
  constexpr char kOperation[] = "exact-authority";
  Ready(kOperation);
  int callbacks = 0;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 3u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++callbacks;
            EXPECT_TRUE(result.has_value());
          }));
  auto confirmation = ProfileBackupCoordinatorTestPeer::ConfirmationOperation(
      *coordinator_, kOperation);
  ASSERT_TRUE(confirmation);
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::DeliverExactStageAuthorization(
      *coordinator_, kOperation));

  ASSERT_EQ(1, target_.stage_calls());
  ASSERT_TRUE(target_.stage_pending());
  ASSERT_TRUE(target_.staged_plan());
  ASSERT_TRUE(target_.staged_authorization());
  EXPECT_TRUE(target_.staged_payload_was_valid());
  EXPECT_TRUE(IsExactBackupRestoreBinding(
      target_.staged_plan()->binding.get(),
      target_.staged_authorization()->binding.get()));
  EXPECT_TRUE(IsExactBackupOperation(
      confirmation.get(),
      target_.staged_authorization()->decision_operation.get()));
  ProfileBackupRestoreStageObservation observation;
  auto procedure = mojom::SkillRecord::New(
      "staged-procedure", "https://example.test",
      mojom::SkillProvenance::kAuthored, mojom::SkillStatus::kDisabled, 2u,
      std::vector<uint8_t>{1u, 2u}, 1u, 7u, 9u);
  observation.procedures.push_back(procedure.Clone());
  target_.CompleteStage(std::move(observation));
  EXPECT_EQ(1, callbacks);
  const auto retained = ProfileBackupCoordinatorTestPeer::StagedProcedures(
      *coordinator_, kOperation);
  ASSERT_EQ(retained.size(), 1u);
  EXPECT_TRUE(retained[0]->Equals(*procedure));
  SettleCancellation(kOperation);
}

TEST_F(ProfileBackupCoordinatorStagingTest,
       InlineStageSuccessMayDestroyCoordinator) {
  constexpr char kOperation[] = "inline-stage-success";
  Ready(kOperation);
  target_.SucceedStageInline();
  int callbacks = 0;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 3u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++callbacks;
            EXPECT_TRUE(result.has_value());
            coordinator_.reset();
          }));

  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::DeliverExactStageAuthorization(
      *coordinator_, kOperation));
  EXPECT_EQ(1, callbacks);
  EXPECT_EQ(1, target_.stage_calls());
  EXPECT_FALSE(target_.owner_live());
}

TEST_F(ProfileBackupCoordinatorStagingTest,
       InlineStageFailureBeginsCleanupWithoutReenteringOwner) {
  constexpr char kOperation[] = "inline-stage-failure";
  Ready(kOperation);
  target_.FailStageInline(ProfileBackupRestoreTargetError::kPayloadMismatch);
  int callbacks = 0;
  std::optional<ProfileBackupError> error;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 3u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++callbacks;
            ASSERT_FALSE(result);
            error = result.error();
          }));

  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::DeliverExactStageAuthorization(
      *coordinator_, kOperation));
  EXPECT_EQ(1, callbacks);
  ASSERT_TRUE(error);
  EXPECT_EQ(ProfileBackupError::kSnapshotMismatch, *error);
  EXPECT_EQ(1, target_.stage_calls());
  ASSERT_TRUE(target_.cleanup_pending());
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  target_.CompleteCleanup(true);
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorStagingTest,
       DuplicateConfirmationIsBusyWhileFirstDecisionIsPending) {
  constexpr char kOperation[] = "duplicate-confirmation";
  Ready(kOperation);
  int first_callbacks = 0;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 3u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++first_callbacks;
            EXPECT_TRUE(result.has_value());
          }));
  int duplicate_callbacks = 0;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 3u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++duplicate_callbacks;
            ASSERT_FALSE(result);
            EXPECT_EQ(ProfileBackupError::kBusy, result.error());
          }));

  EXPECT_EQ(0, first_callbacks);
  EXPECT_EQ(1, duplicate_callbacks);
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::DeliverExactStageAuthorization(
      *coordinator_, kOperation));
  ASSERT_EQ(1, target_.stage_calls());
  target_.CompleteStage(ProfileBackupRestoreStageObservation{});
  EXPECT_EQ(1, first_callbacks);
  SettleCancellation(kOperation);
}

TEST_F(ProfileBackupCoordinatorStagingTest,
       CancelDuringConfirmationPreventsLatePhysicalStage) {
  constexpr char kOperation[] = "cancel-confirmation";
  Ready(kOperation);
  int callbacks = 0;
  std::optional<ProfileBackupError> error;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 3u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++callbacks;
            ASSERT_FALSE(result);
            error = result.error();
          }));
  coordinator_->Cancel(kOperation);

  EXPECT_EQ(1, callbacks);
  ASSERT_TRUE(error);
  EXPECT_EQ(ProfileBackupError::kCancelled, *error);
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::DeliverExactStageAuthorization(
      *coordinator_, kOperation));
  EXPECT_EQ(0, target_.stage_calls());
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  ASSERT_TRUE(target_.cleanup_pending());
  target_.CompleteCleanup(true);
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

TEST_F(ProfileBackupCoordinatorStagingTest,
       CancelPendingStageSettlesCallbackExactlyOnce) {
  constexpr char kOperation[] = "cancel-physical-stage";
  Ready(kOperation);
  int callbacks = 0;
  std::optional<ProfileBackupError> error;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 3u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++callbacks;
            if (!result) {
              error = result.error();
            }
          }));
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::DeliverExactStageAuthorization(
      *coordinator_, kOperation));
  ASSERT_TRUE(target_.stage_pending());

  coordinator_->Cancel(kOperation);
  EXPECT_EQ(1, callbacks);
  ASSERT_TRUE(error);
  EXPECT_EQ(ProfileBackupError::kCancelled, *error);
  target_.CompleteStage(ProfileBackupRestoreStageObservation{});
  EXPECT_EQ(1, callbacks);
  ProfileBackupCoordinatorTestPeer::DeliverCancellation(
      *coordinator_, kOperation,
      mojom::BackupRestoreProtocolStatus::kSucceeded);
  ASSERT_TRUE(target_.cleanup_pending());
  target_.CompleteCleanup(true);
  EXPECT_EQ(1, callbacks);
}

TEST_F(ProfileBackupCoordinatorStagingTest,
       SourceDisconnectAllowsInlineCleanupToDestroyTarget) {
  constexpr char kOperation[] = "disconnect-physical-stage";
  Ready(kOperation);
  int callbacks = 0;
  std::optional<ProfileBackupError> error;
  coordinator_->ConfirmAndStageImportedRestore(
      kOperation, std::vector<uint8_t>(32u, 3u),
      base::BindLambdaForTesting(
          [&](ProfileBackupCoordinator::RestoreStageResult result) {
            ++callbacks;
            if (!result) {
              error = result.error();
            }
          }));
  ASSERT_TRUE(ProfileBackupCoordinatorTestPeer::DeliverExactStageAuthorization(
      *coordinator_, kOperation));
  target_.CompleteCleanupInline(true);
  CoreServiceManagerTaskEffectTestPeer::Disconnect(*manager_);

  EXPECT_EQ(1, callbacks);
  ASSERT_TRUE(error);
  EXPECT_EQ(ProfileBackupError::kCoreUnavailable, *error);
  EXPECT_EQ(1, target_.cleanup_calls());
  EXPECT_FALSE(target_.stage_pending());
  EXPECT_FALSE(target_.owner_live());
  EXPECT_FALSE(
      ProfileBackupCoordinatorTestPeer::HasImport(*coordinator_, kOperation));
}

}  // namespace
}  // namespace taffy
