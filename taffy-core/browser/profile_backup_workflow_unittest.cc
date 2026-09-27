// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_workflow.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "crypto/secure_util.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/profile_backup_workflow_io.h"
#include "taffy/browser/profile_backup_workflow_internal.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"
#include "taffy/components/storage/browser/backup_recovery_key_codec.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class ProfileBackupWorkflowTestPeer final {
 public:
  static void AddCommitDrain(ProfileBackupWorkflow& workflow,
                             ProfileBackupWorkflow::WindowToken owner,
                             std::string operation_id,
                             int* completion_count) {
    base::AutoLock guard(workflow.state_lock_);
    auto operation = std::make_unique<ProfileBackupWorkflow::Operation>(
        owner, ProfileBackupWorkflow::Operation::Phase::kRestoreCommitting);
    operation->restore_commit_callback = base::BindLambdaForTesting(
        [completion_count](ProfileBackupWorkflow::RestoreCommitStatus) {
          ++*completion_count;
        });
    workflow.operations_.emplace(std::move(operation_id),
                                 std::move(operation));
  }

  static bool IsDetached(const ProfileBackupWorkflow& workflow,
                         const std::string& operation_id) {
    base::AutoLock guard(workflow.state_lock_);
    const auto found = workflow.operations_.find(operation_id);
    return found != workflow.operations_.end() &&
           found->second->restore_window_detached &&
           found->second->phase ==
               ProfileBackupWorkflow::Operation::Phase::kRestoreCommitting;
  }

  static void CompleteClose(ProfileBackupWorkflow& workflow,
                            const std::string& operation_id) {
    {
      base::AutoLock guard(workflow.state_lock_);
      auto found = workflow.operations_.find(operation_id);
      ASSERT_TRUE(found != workflow.operations_.end());
      found->second->phase =
          ProfileBackupWorkflow::Operation::Phase::kRestoreClosing;
    }
    workflow.OnImportedRestoreClosed(
        operation_id,
        ProfileBackupWorkflow::RestoreCommitStatus::kRecoveryRequired,
        base::ok());
  }

};

namespace {

namespace core_mojom = core_service::mojom;

constexpr char kInstallationId[] = "08e23c21-a9ee-4c04-9357-673a6de6a82c";

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

class ProfileBackupWorkflowTest : public ::testing::Test {
 protected:
  void SetUp() override {
    context_ = std::make_unique<content::TestBrowserContext>();
    manager_ = MakeManager(tail_, context_.get());
    ASSERT_TRUE(staging_parent_.CreateUniqueTempDir());
    stage_store_ = storage::backup::BackupArchiveStageStore::Create(
        staging_parent_.GetPath().Append(
            storage::backup::kBackupStagingDirectoryName));
    ASSERT_TRUE(stage_store_);
    workflow_ = std::make_unique<ProfileBackupWorkflow>(manager_.get(),
                                                        kInstallationId);
    ASSERT_TRUE(workflow_->InstallStageStore(stage_store_));
  }

  void TearDown() override {
    workflow_.reset();
    manager_->Shutdown();
    task_environment_.RunUntilIdle();
  }

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<content::TestBrowserContext> context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  base::ScopedTempDir staging_parent_;
  scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store_;
  std::unique_ptr<ProfileBackupWorkflow> workflow_;
};

TEST_F(ProfileBackupWorkflowTest,
       WindowActivationIsExclusiveAndBoundsNewAuthority) {
  const auto first = workflow_->RegisterWindow();
  const auto second = workflow_->RegisterWindow();
  ASSERT_NE(0u, first);
  ASSERT_NE(0u, second);

  EXPECT_FALSE(workflow_->OpenRecoveryKeySession(
      first, ProfileBackupWorkflow::KeyMode::kCreate));
  EXPECT_TRUE(workflow_->ActivateWindow(first));
  EXPECT_FALSE(workflow_->ActivateWindow(second));
  EXPECT_TRUE(workflow_->OpenRecoveryKeySession(
      first, ProfileBackupWorkflow::KeyMode::kCreate));

  workflow_->DeactivateWindow(first);
  EXPECT_TRUE(workflow_->ActivateWindow(second));
  EXPECT_FALSE(workflow_->OpenRecoveryKeySession(
      first, ProfileBackupWorkflow::KeyMode::kRestore));
}

TEST_F(ProfileBackupWorkflowTest,
       GeneratedKeyIsOneShotAndExactWindowWithdrawalWipesTheOperation) {
  const auto owner = workflow_->RegisterWindow();
  const auto other = workflow_->RegisterWindow();
  ASSERT_TRUE(workflow_->ActivateWindow(owner));
  auto opened = workflow_->OpenRecoveryKeySession(
      owner, ProfileBackupWorkflow::KeyMode::kCreate);
  ASSERT_TRUE(opened);
  const std::string operation = *opened;

  EXPECT_FALSE(workflow_->TakeGeneratedKeyForDisplay(other, operation));
  EXPECT_TRUE(workflow_->TakeGeneratedKeyForDisplay(owner, operation));
  EXPECT_FALSE(workflow_->TakeGeneratedKeyForDisplay(owner, operation));
  EXPECT_EQ(ProfileBackupWorkflow::KeyAcceptance::kAccepted,
            workflow_->ConfirmKeyRetained(owner, operation));
  EXPECT_EQ(ProfileBackupWorkflow::KeyAcceptance::kRefused,
            workflow_->ConfirmKeyRetained(owner, operation));
  EXPECT_EQ(1u, workflow_->operation_count_for_testing());

  workflow_->UnregisterWindow(owner);
  EXPECT_EQ(0u, workflow_->operation_count_for_testing());
  EXPECT_EQ(ProfileBackupWorkflow::KeyAcceptance::kUnavailable,
            workflow_->ConfirmKeyRetained(owner, operation));
}

TEST_F(ProfileBackupWorkflowTest,
       RestoreKeyCreatesOnlyAnExactBoundedEncryptedImportStage) {
  const auto owner = workflow_->RegisterWindow();
  const auto other = workflow_->RegisterWindow();
  ASSERT_TRUE(workflow_->ActivateWindow(owner));
  auto opened = workflow_->OpenRecoveryKeySession(
      owner, ProfileBackupWorkflow::KeyMode::kRestore);
  ASSERT_TRUE(opened);
  const std::string operation = *opened;
  const std::array<char16_t, 3> malformed = {u'b', u'a', u'd'};
  EXPECT_EQ(ProfileBackupWorkflow::KeyAcceptance::kRefused,
            workflow_->AcceptEnteredKey(owner, operation, malformed));

  storage::backup::Secret key = storage::backup::GenerateSecret();
  auto displayed = storage::backup::FormatRecoveryKeyForDisplay(key);
  ASSERT_TRUE(displayed);
  EXPECT_EQ(ProfileBackupWorkflow::KeyAcceptance::kAccepted,
            workflow_->AcceptEnteredKey(owner, operation, *displayed));
  crypto::SecureZeroBuffer(key);
  // The key text is UTF-16, and SecureZeroBuffer wipes a byte span, so
  // the whole array is handed over reinterpreted as its bytes.
  crypto::SecureZeroBuffer(base::as_writable_byte_span(*displayed));
  EXPECT_FALSE(workflow_->MaximumImportBytes(other, operation));
  const auto maximum = workflow_->MaximumImportBytes(owner, operation);
  ASSERT_TRUE(maximum);
  EXPECT_EQ(storage::backup::BackupArchiveStageStore::MaximumArchiveBytes(),
            *maximum);

  workflow_->DeactivateWindow(owner);
  const auto paused_maximum = workflow_->MaximumImportBytes(owner, operation);
  ASSERT_TRUE(paused_maximum);
  EXPECT_EQ(storage::backup::BackupArchiveStageStore::MaximumArchiveBytes(),
            *paused_maximum);
  workflow_->AbandonOperation(owner, operation);
  EXPECT_FALSE(workflow_->MaximumImportBytes(owner, operation));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0u, stage_store_->size_for_testing());
}

TEST_F(ProfileBackupWorkflowTest,
       DetachedInspectionCannotPublishAfterItsWindowIsWithdrawn) {
  const auto owner = workflow_->RegisterWindow();
  ASSERT_TRUE(workflow_->ActivateWindow(owner));
  auto opened = workflow_->OpenRecoveryKeySession(
      owner, ProfileBackupWorkflow::KeyMode::kRestore);
  ASSERT_TRUE(opened);
  const std::string operation = *opened;
  storage::backup::Secret key = storage::backup::GenerateSecret();
  auto displayed = storage::backup::FormatRecoveryKeyForDisplay(key);
  ASSERT_TRUE(displayed);
  ASSERT_EQ(ProfileBackupWorkflow::KeyAcceptance::kAccepted,
            workflow_->AcceptEnteredKey(owner, operation, *displayed));
  crypto::SecureZeroBuffer(key);
  crypto::SecureZeroBuffer(base::as_writable_byte_span(*displayed));

  auto handle =
      workflow_->AcquireImportInspection(owner, operation, /*actual_bytes=*/1u);
  ASSERT_TRUE(handle);
  EXPECT_FALSE(workflow_->AcquireImportInspection(owner, operation,
                                                  /*actual_bytes=*/1u));
  handle.reset();
  handle =
      workflow_->AcquireImportInspection(owner, operation, /*actual_bytes=*/1u);
  ASSERT_TRUE(handle);
  workflow_->UnregisterWindow(owner);
  handle->Run();

  ASSERT_TRUE(handle->result());
  EXPECT_EQ(storage::backup::BackupStageStatus::kUnavailable,
            *handle->result());
  EXPECT_EQ(storage::backup::BackupStageStatus::kUnavailable,
            workflow_->CompleteImportInspection(owner, operation, *handle));
}

TEST_F(ProfileBackupWorkflowTest,
       DetachedInspectionRetainsExclusiveStageDirectoryLeaseAfterClose) {
  const auto owner = workflow_->RegisterWindow();
  ASSERT_TRUE(workflow_->ActivateWindow(owner));
  auto opened = workflow_->OpenRecoveryKeySession(
      owner, ProfileBackupWorkflow::KeyMode::kRestore);
  ASSERT_TRUE(opened);
  storage::backup::Secret key = storage::backup::GenerateSecret();
  auto displayed = storage::backup::FormatRecoveryKeyForDisplay(key);
  ASSERT_TRUE(displayed);
  ASSERT_EQ(ProfileBackupWorkflow::KeyAcceptance::kAccepted,
            workflow_->AcceptEnteredKey(owner, *opened, *displayed));
  crypto::SecureZeroBuffer(key);
  crypto::SecureZeroBuffer(base::as_writable_byte_span(*displayed));
  auto handle =
      workflow_->AcquireImportInspection(owner, *opened, /*actual_bytes=*/1u);
  ASSERT_TRUE(handle);
  const base::FilePath staging_directory = staging_parent_.GetPath().Append(
      storage::backup::kBackupStagingDirectoryName);

  workflow_.reset();
  stage_store_.reset();
  // Remove the cleanup worker's reference before checking the detached
  // inspection handle, so only that handle can still own the directory.
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(
      storage::backup::BackupArchiveStageStore::Create(staging_directory));
  handle.reset();
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(
      storage::backup::BackupArchiveStageStore::Create(staging_directory));
}

TEST_F(ProfileBackupWorkflowTest,
       PendingCleanupRetainsExclusiveStageDirectoryLeaseAfterClose) {
  const base::FilePath staging_directory = staging_parent_.GetPath().Append(
      storage::backup::kBackupStagingDirectoryName);
  {
    base::test::TaskEnvironment::ParallelExecutionFence cleanup_fence;
    workflow_.reset();
    stage_store_.reset();
    EXPECT_FALSE(
        storage::backup::BackupArchiveStageStore::Create(staging_directory));
  }
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(
      storage::backup::BackupArchiveStageStore::Create(staging_directory));
}

TEST_F(ProfileBackupWorkflowTest,
       SelectionAdmissionUsesTheSharedSixKindStorageBoundary) {
  const auto owner = workflow_->RegisterWindow();
  ASSERT_TRUE(workflow_->ActivateWindow(owner));
  auto opened = workflow_->OpenRecoveryKeySession(
      owner, ProfileBackupWorkflow::KeyMode::kCreate);
  ASSERT_TRUE(opened);
  const std::string operation = *opened;
  ASSERT_TRUE(workflow_->TakeGeneratedKeyForDisplay(owner, operation));
  ASSERT_EQ(ProfileBackupWorkflow::KeyAcceptance::kAccepted,
            workflow_->ConfirmKeyRetained(owner, operation));
  // A kind outside the shared six is refused by the workflow itself, before a
  // key is spent or the coordinator is told anything, so its callback is
  // dropped rather than answered.
  auto never_answered =
      base::BindOnce([](ProfileBackupWorkflow::ExportPreparation) {
        ADD_FAILURE() << "A refused selection must not publish a preparation";
      });
  EXPECT_FALSE(workflow_->PrepareExport(
      owner, operation, {core_mojom::BackupRecordKind::kBookmark},
      std::move(never_answered)));
  EXPECT_EQ(1u, workflow_->operation_count_for_testing());

  // The six are admitted and reach the coordinator, which is the whole point
  // of the boundary. This fixture has no ready core service, so the refusal
  // comes back on the caller's own stack while the window is still active;
  // that is a published preparation, and a correct one. The status is the
  // coordinator's, not the storage boundary's.
  int published = 0;
  auto status = ProfileBackupWorkflow::ExportPreparationStatus::kReady;
  EXPECT_TRUE(workflow_->PrepareExport(
      owner, operation,
      {core_mojom::BackupRecordKind::kLearnedProcedure,
       core_mojom::BackupRecordKind::kAssistantConfiguration,
       core_mojom::BackupRecordKind::kMemoryRecord,
       core_mojom::BackupRecordKind::kSavedWorkspace,
       core_mojom::BackupRecordKind::kUserAuthoredSkill,
       core_mojom::BackupRecordKind::kLibraryEntry},
      base::BindLambdaForTesting(
          [&](ProfileBackupWorkflow::ExportPreparation result) {
            ++published;
            status = result.status;
          })));
  EXPECT_EQ(1, published);
  EXPECT_NE(ProfileBackupWorkflow::ExportPreparationStatus::kReady, status)
      << "no core service is ready in this fixture";

  workflow_->UnregisterWindow(owner);
  EXPECT_EQ(0u, workflow_->operation_count_for_testing());
}

TEST_F(ProfileBackupWorkflowTest,
       WindowWithdrawalRetainsOnlyConsumedCommitUntilWriterCloseSettles) {
  const auto owner = workflow_->RegisterWindow();
  ASSERT_NE(0u, owner);
  constexpr char kDrainingRestore[] = "draining-restore";
  int completion_count = 0;
  ProfileBackupWorkflowTestPeer::AddCommitDrain(*workflow_, owner,
                                                kDrainingRestore,
                                                &completion_count);

  workflow_->UnregisterWindow(owner);
  EXPECT_EQ(1u, workflow_->operation_count_for_testing());
  EXPECT_TRUE(
      ProfileBackupWorkflowTestPeer::IsDetached(*workflow_, kDrainingRestore));

  ProfileBackupWorkflowTestPeer::CompleteClose(*workflow_, kDrainingRestore);
  EXPECT_EQ(0u, workflow_->operation_count_for_testing());
  EXPECT_EQ(1, completion_count);
}

}  // namespace
}  // namespace taffy
