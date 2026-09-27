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

#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/profile_backup_workflow.h"
#include "taffy/browser/profile_backup_workflow_internal.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class ProfileBackupWorkflowPresentationTestPeer final {
 public:
  static void Begin(
      ProfileBackupWorkflow& workflow,
      ProfileBackupWorkflow::WindowToken owner,
      std::string operation_id,
      ProfileBackupWorkflow::RestorePresentationWriter writer,
      ProfileBackupWorkflow::RestorePreparationCallback callback) {
    base::AutoLock guard(workflow.state_lock_);
    auto operation = std::make_unique<ProfileBackupWorkflow::Operation>(
        owner, ProfileBackupWorkflow::Operation::Phase::kRestorePlanning);
    operation->reservation_id = "presentation-reservation";
    operation->target_profile_label = u"Restored profile";
    operation->restore_presentation_writer = std::move(writer);
    operation->restore_preparation_callback = std::move(callback);
    ASSERT_TRUE(workflow.operations_
                    .emplace(std::move(operation_id), std::move(operation))
                    .second);
  }

  static void DeliverPlan(ProfileBackupWorkflow& workflow,
                          const std::string& operation_id,
                          ProfileBackupRestorePreview preview) {
    workflow.OnImportedRestorePlanned(operation_id, std::move(preview));
  }

  static bool IsPersisting(const ProfileBackupWorkflow& workflow,
                           const std::string& operation_id) {
    base::AutoLock guard(workflow.state_lock_);
    return workflow.operations_.at(operation_id)->phase ==
           ProfileBackupWorkflow::Operation::Phase::
               kRestorePresentationPersisting;
  }

  static bool IsPrecommitCleanupRequired(const ProfileBackupWorkflow& workflow,
                                         const std::string& operation_id) {
    base::AutoLock guard(workflow.state_lock_);
    return workflow.operations_.at(operation_id)->phase ==
           ProfileBackupWorkflow::Operation::Phase::kPrecommitCleanupRequired;
  }

  static ProfileBackupWorkflow::RestoreReviewToken ReviewToken(
      const ProfileBackupWorkflow& workflow,
      const std::string& operation_id) {
    base::AutoLock guard(workflow.state_lock_);
    return workflow.operations_.at(operation_id)->restore_review_token;
  }
};

namespace {

namespace mojom = core_service::mojom;
using Kind = mojom::BackupRecordKind;

constexpr char kInstallationId[] = "08e23c21-a9ee-4c04-9357-673a6de6a82c";
constexpr char kSource[] = "11111111-1111-4111-8111-111111111111";
constexpr char kTarget[] = "22222222-2222-4222-8222-222222222222";
constexpr char kOperationId[] = "workflow-presentation";

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

mojom::BackupRestorePlanResultPtr Plan() {
  auto operation = mojom::OperationEnvelope::New(
      "presentation-plan", 7u, 0u,
      base::TimeTicks::Now().since_origin().InMilliseconds() + 60'000u,
      "presentation-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, kTarget);
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "presentation-archive";
  plan->snapshot_sha256.assign(32u, 1u);
  plan->target = target.Clone();
  plan->entries.push_back(mojom::BackupRestorePlanEntry::New(
      Kind::kMemoryRecord, "memory-1", 1u,
      mojom::BackupRestoreAction::kStageCreate, 1u,
      mojom::BackupRecordState::kActive, 4u, std::vector<uint8_t>(32u, 3u)));
  plan->has_conflicts = false;
  plan->confirmation_sha256.assign(32u, 2u);
  plan->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), kSource, std::move(target), plan->backup_id,
      plan->snapshot_sha256, plan->confirmation_sha256);
  return plan;
}

ProfileBackupRestorePreview Preview(std::vector<Kind> selection = {
                                        Kind::kMemoryRecord,
                                        Kind::kAssistantConfiguration}) {
  ProfileBackupRestorePreview preview;
  preview.plan = Plan();
  preview.selection = std::move(selection);
  return preview;
}

class PresentationWriterState {
 public:
  ProfileBackupWorkflow::RestorePresentationWriter TakeWriter() {
    return base::BindLambdaForTesting(
        [this](std::string reservation_id, std::u16string label,
               std::vector<Kind> selection,
               mojom::BackupRestorePlanResultPtr plan,
               ProfileBackupWorkflow::RestorePresentationCallback callback) {
          ++call_count;
          reservation = std::move(reservation_id);
          target_label = std::move(label);
          original_selection = std::move(selection);
          exact_plan = std::move(plan);
          completion = std::move(callback);
        });
  }

  void Complete(BackupRestoreRecoveryPresentationResult result) {
    ASSERT_TRUE(completion);
    std::move(completion).Run(std::move(result));
  }

  int call_count = 0;
  std::string reservation;
  std::u16string target_label;
  std::vector<Kind> original_selection;
  mojom::BackupRestorePlanResultPtr exact_plan;
  ProfileBackupWorkflow::RestorePresentationCallback completion;
};

class ProfileBackupWorkflowPresentationTest : public testing::Test {
 protected:
  void SetUp() override {
    context_ = std::make_unique<content::TestBrowserContext>();
    manager_ = MakeManager(manager_tail_, context_.get());
    workflow_ = std::make_unique<ProfileBackupWorkflow>(manager_.get(),
                                                        kInstallationId);
  }

  void TearDown() override {
    workflow_.reset();
    manager_->Shutdown();
    environment_.RunUntilIdle();
  }

  void Begin(PresentationWriterState& writer) {
    ProfileBackupWorkflowPresentationTestPeer::Begin(
        *workflow_, 1u, kOperationId, writer.TakeWriter(),
        base::BindLambdaForTesting(
            [this](ProfileBackupWorkflow::RestorePreparation result) {
              ++preparation_count_;
              preparation_ = std::move(result);
            }));
  }

  content::BrowserTaskEnvironment environment_;
  std::unique_ptr<content::TestBrowserContext> context_;
  test::QuietManagerTail manager_tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  std::unique_ptr<ProfileBackupWorkflow> workflow_;
  int preparation_count_ = 0;
  std::optional<ProfileBackupWorkflow::RestorePreparation> preparation_;
};

TEST_F(ProfileBackupWorkflowPresentationTest,
       ReviewRemainsPrivateUntilExactDurablePresentationReturns) {
  PresentationWriterState writer;
  Begin(writer);
  auto preview = Preview();
  auto expected = backup_restore_recovery_presentation_internal::Build(
      u"Restored profile", preview.selection, *preview.plan);
  ASSERT_TRUE(expected);

  ProfileBackupWorkflowPresentationTestPeer::DeliverPlan(
      *workflow_, kOperationId, std::move(preview));

  EXPECT_EQ(1, writer.call_count);
  EXPECT_EQ("presentation-reservation", writer.reservation);
  EXPECT_EQ(u"Restored profile", writer.target_label);
  EXPECT_EQ(
      (std::vector<Kind>{Kind::kMemoryRecord, Kind::kAssistantConfiguration}),
      writer.original_selection);
  ASSERT_TRUE(writer.exact_plan);
  EXPECT_EQ(0, preparation_count_);
  EXPECT_EQ(0u, ProfileBackupWorkflowPresentationTestPeer::ReviewToken(
                    *workflow_, kOperationId));
  EXPECT_TRUE(ProfileBackupWorkflowPresentationTestPeer::IsPersisting(
      *workflow_, kOperationId));

  writer.Complete(std::move(*expected));

  ASSERT_EQ(1, preparation_count_);
  ASSERT_TRUE(preparation_);
  EXPECT_EQ(ProfileBackupWorkflow::RestorePreparationStatus::kReady,
            preparation_->status);
  EXPECT_NE(0u, preparation_->review_token);
  ASSERT_EQ(2u, preparation_->selected_classes.size());
  EXPECT_EQ(Kind::kAssistantConfiguration,
            preparation_->selected_classes[0].kind);
  EXPECT_EQ((std::array<uint32_t, 6>{}),
            preparation_->selected_classes[0].action_counts);
  EXPECT_EQ(Kind::kMemoryRecord, preparation_->selected_classes[1].kind);
  EXPECT_EQ((std::array<uint32_t, 6>{1u, 0u, 0u, 0u, 0u, 0u}),
            preparation_->selected_classes[1].action_counts);
}

TEST_F(ProfileBackupWorkflowPresentationTest,
       MismatchedPersistedPresentationNeverMintsReview) {
  PresentationWriterState writer;
  Begin(writer);
  auto preview = Preview();
  auto expected = backup_restore_recovery_presentation_internal::Build(
      u"Restored profile", preview.selection, *preview.plan);
  ASSERT_TRUE(expected);
  expected->target_profile_label = u"Different profile";
  ProfileBackupWorkflowPresentationTestPeer::DeliverPlan(
      *workflow_, kOperationId, std::move(preview));

  writer.Complete(std::move(*expected));

  ASSERT_EQ(1, preparation_count_);
  ASSERT_TRUE(preparation_);
  EXPECT_EQ(ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable,
            preparation_->status);
  EXPECT_EQ(0u, preparation_->review_token);
  EXPECT_TRUE(
      ProfileBackupWorkflowPresentationTestPeer::IsPrecommitCleanupRequired(
          *workflow_, kOperationId));
}

TEST_F(ProfileBackupWorkflowPresentationTest,
       InlinePersistenceCompletionIsReentrantAndExactlyOnce) {
  auto preview = Preview({Kind::kMemoryRecord});
  auto expected = backup_restore_recovery_presentation_internal::Build(
      u"Restored profile", preview.selection, *preview.plan);
  ASSERT_TRUE(expected);
  auto writer = base::BindOnce(
      [](BackupRestoreRecoveryPresentation presentation, std::string,
         std::u16string, std::vector<Kind>, mojom::BackupRestorePlanResultPtr,
         ProfileBackupWorkflow::RestorePresentationCallback callback) {
        std::move(callback).Run(
            BackupRestoreRecoveryPresentationResult(std::move(presentation)));
      },
      std::move(*expected));
  ProfileBackupWorkflowPresentationTestPeer::Begin(
      *workflow_, 1u, kOperationId, std::move(writer),
      base::BindLambdaForTesting(
          [this](ProfileBackupWorkflow::RestorePreparation result) {
            ++preparation_count_;
            preparation_ = std::move(result);
          }));

  ProfileBackupWorkflowPresentationTestPeer::DeliverPlan(
      *workflow_, kOperationId, std::move(preview));

  ASSERT_EQ(1, preparation_count_);
  ASSERT_TRUE(preparation_);
  EXPECT_EQ(ProfileBackupWorkflow::RestorePreparationStatus::kReady,
            preparation_->status);
}

TEST_F(ProfileBackupWorkflowPresentationTest,
       WithdrawalDuringPersistenceCannotMintLateReview) {
  PresentationWriterState writer;
  Begin(writer);
  auto preview = Preview();
  auto expected = backup_restore_recovery_presentation_internal::Build(
      u"Restored profile", preview.selection, *preview.plan);
  ASSERT_TRUE(expected);
  ProfileBackupWorkflowPresentationTestPeer::DeliverPlan(
      *workflow_, kOperationId, std::move(preview));
  ASSERT_TRUE(ProfileBackupWorkflowPresentationTestPeer::IsPersisting(
      *workflow_, kOperationId));

  workflow_->AbandonOperation(1u, kOperationId);
  writer.Complete(std::move(*expected));

  EXPECT_EQ(0, preparation_count_);
  EXPECT_FALSE(preparation_);
  EXPECT_EQ(0u, workflow_->operation_count_for_testing());
}

}  // namespace
}  // namespace taffy
