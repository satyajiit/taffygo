// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_command_validation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class CountingEntropy final : public CoreApiEntropySource {
 public:
  std::string NewOpaqueId(std::string_view domain) override {
    ++calls;
    return std::string(domain) + "-fixed";
  }

  std::array<uint8_t, 32> NewTaskSeed() override { return {}; }

  size_t calls = 0u;
};

TEST(CoreApiCommandFactoryControlsTest, EveryControlKeepsItsExactTypedBody) {
  CoreApiCommandFactory pause_factory("profile",
                                      std::make_unique<CountingEntropy>());
  auto pause = pause_factory.BuildPauseTask("task-1", 7u, 11u, 100u);
  ASSERT_TRUE(pause);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kPauseTask,
            pause->core_api_command->kind);
  ASSERT_TRUE(pause->core_api_command->pause_task);
  EXPECT_EQ(core_service::mojom::CoreServiceCommandKind::kPauseTask,
            pause->core_service_command->kind);
  ASSERT_TRUE(pause->core_service_command->pause_task);
  EXPECT_EQ(7u, pause->core_service_command->operation->task_revision);
  EXPECT_EQ(11u, pause->core_service_command->operation->service_generation);

  CoreApiCommandFactory resume_factory("profile",
                                       std::make_unique<CountingEntropy>());
  auto resume = resume_factory.BuildResumeTask("task-1", 8u, 12u, 100u);
  ASSERT_TRUE(resume);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kResumeTask,
            resume->core_api_command->kind);
  ASSERT_TRUE(resume->core_service_command->resume_task);
  EXPECT_EQ(core_service::mojom::CoreServiceCommandKind::kResumeTask,
            resume->core_service_command->kind);

  CoreApiCommandFactory takeover_factory("profile",
                                         std::make_unique<CountingEntropy>());
  auto takeover = takeover_factory.BuildTakeOver("task-1", 9u, 13u, 100u);
  ASSERT_TRUE(takeover);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kTakeOver,
            takeover->core_api_command->kind);
  ASSERT_TRUE(takeover->core_service_command->take_over);
  EXPECT_EQ(core_service::mojom::CoreServiceCommandKind::kTakeOver,
            takeover->core_service_command->kind);
}

TEST(CoreApiCommandFactoryControlsTest,
     InvalidBindingMintsNoOperationOrTraceEntropy) {
  auto entropy = std::make_unique<CountingEntropy>();
  CountingEntropy* entropy_view = entropy.get();
  CoreApiCommandFactory factory("profile", std::move(entropy));

  EXPECT_FALSE(factory.BuildPauseTask("", 7u, 11u, 100u));
  EXPECT_FALSE(factory.BuildResumeTask("task-1", 0u, 11u, 100u));
  EXPECT_FALSE(factory.BuildTakeOver(std::string(257u, 'x'), 7u, 11u, 100u));
  EXPECT_FALSE(factory.BuildSaveWorkspace("", 7u, 11u, 100u));
  EXPECT_EQ(0u, entropy_view->calls);
}

TEST(CoreApiCommandFactoryControlsTest,
     ExplicitWorkspaceSaveKeepsExactRevisionOnBothContracts) {
  CoreApiCommandFactory factory("profile", std::make_unique<CountingEntropy>());
  auto projected = factory.BuildSaveWorkspace("workspace-1", 9u, 13u, 100u);
  ASSERT_TRUE(projected);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kSaveWorkspace,
            projected->core_api_command->kind);
  ASSERT_TRUE(projected->core_api_command->save_workspace);
  EXPECT_EQ(9u, projected->core_api_command->save_workspace->expected_revision);
  EXPECT_EQ(core_service::mojom::CoreServiceCommandKind::kSaveWorkspace,
            projected->core_service_command->kind);
  ASSERT_TRUE(projected->core_service_command->save_workspace);
  EXPECT_EQ("workspace-1",
            projected->core_service_command->save_workspace->workspace_id);
  EXPECT_EQ(9u,
            projected->core_service_command->save_workspace->expected_revision);
  EXPECT_EQ(0u, projected->core_service_command->operation->task_revision);
}

TEST(CoreApiCommandFactoryControlsTest,
     WorkspaceLifecycleKeepsExactRevisionNameAndConfirmation) {
  CoreApiCommandFactory rename_factory("profile",
                                       std::make_unique<CountingEntropy>());
  auto rename = rename_factory.BuildRenameWorkspace(
      "workspace-1", 9u, "Evidence comparison", 13u, 100u);
  ASSERT_TRUE(rename);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kRenameWorkspace,
            rename->core_api_command->kind);
  ASSERT_TRUE(rename->core_api_command->rename_workspace);
  EXPECT_EQ(9u, rename->core_api_command->rename_workspace->expected_revision);
  EXPECT_EQ("Evidence comparison",
            rename->core_service_command->rename_workspace->display_name);

  const std::string confirmation_token(64u, 'a');
  CoreApiCommandFactory delete_factory("profile",
                                       std::make_unique<CountingEntropy>());
  auto deletion = delete_factory.BuildDeleteWorkspace(
      "workspace-1", 10u, confirmation_token, 13u, 100u);
  ASSERT_TRUE(deletion);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kDeleteWorkspace,
            deletion->core_api_command->kind);
  ASSERT_TRUE(deletion->core_service_command->delete_workspace);
  EXPECT_EQ(
      10u, deletion->core_service_command->delete_workspace->expected_revision);
  EXPECT_EQ(
      confirmation_token,
      deletion->core_service_command->delete_workspace->confirmation_token);

  CoreApiCommandFactory discard_factory("profile",
                                        std::make_unique<CountingEntropy>());
  auto discard =
      discard_factory.BuildDiscardWorkspace("workspace-2", 4u, 13u, 100u);
  ASSERT_TRUE(discard);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kDiscardWorkspace,
            discard->core_api_command->kind);
  EXPECT_EQ(
      4u, discard->core_service_command->discard_workspace->expected_revision);
}

TEST(CoreApiCommandFactoryControlsTest,
     InvalidWorkspaceLifecycleInputMintsNoOperationOrTraceEntropy) {
  auto entropy = std::make_unique<CountingEntropy>();
  CountingEntropy* entropy_view = entropy.get();
  CoreApiCommandFactory factory("profile", std::move(entropy));

  EXPECT_FALSE(
      factory.BuildRenameWorkspace("workspace-1", 7u, " hidden", 11u, 100u));
  EXPECT_FALSE(factory.BuildDeleteWorkspace("workspace-1", 7u,
                                            std::string(63u, 'a'), 11u, 100u));
  EXPECT_FALSE(factory.BuildDiscardWorkspace("", 7u, 11u, 100u));
  EXPECT_EQ(0u, entropy_view->calls);
}

TEST(CoreApiCommandFactoryControlsTest,
     GeneratedKindSafetyIsClosedAndExhaustive) {
  using Kind = core_api::mojom::CoreCommandKind;
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kStartTask));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kCancelTask));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kCompleteHandover));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kSupplyUserInput));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kFollowUp));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kApproveAction));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kRetryCore));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kPermissionResult));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kStartAuth));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kRequestEmailLink));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kSignOut));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kAuthCredentialResult));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kCorrectWorkspaceFact));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kExcludeWorkspaceSource));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kRequestWorkspaceExport));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kSaveWorkspace));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kRenameWorkspace));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kDeleteWorkspace));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kDiscardWorkspace));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kSearchLibrary));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kSaveLibraryFact));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kRemoveLibraryEntry));
  EXPECT_TRUE(
      CoreApiCommandFactory::IsSafeCommandKind(Kind::kRequestLibraryExport));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kSearchMemory));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kUpsertMemory));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kDeleteMemory));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kRequestAsset));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kRemoveAsset));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(Kind::kSetAssetPolicy));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(
      Kind::kRequestComposerCompletion));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(
      Kind::kCancelComposerCompletion));
  EXPECT_TRUE(CoreApiCommandFactory::IsSafeCommandKind(
      Kind::kSetAssistantConfiguration));
}

}  // namespace
}  // namespace taffy
