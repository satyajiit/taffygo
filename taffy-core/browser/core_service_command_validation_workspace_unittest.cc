// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_command_validation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

bool IsValid(const ProjectedCoreCommand& command) {
  return IsStructurallyValidCoreServiceCommand(
      *command.core_service_command, core_service::mojom::kMaxCommandBytes);
}

TEST(CoreServiceCommandValidationWorkspaceTest,
     AdmitsEveryWorkspaceLifecycleCommand) {
  CoreApiCommandFactory factory("profile", CreateCoreApiEntropySource());
  auto save = factory.BuildSaveWorkspace("workspace-1", 9u, 13u, 100u);
  auto rename = factory.BuildRenameWorkspace("workspace-1", 9u,
                                             "Evidence comparison", 13u, 100u);
  auto deletion = factory.BuildDeleteWorkspace(
      "workspace-1", 10u, std::string(64u, 'a'), 13u, 100u);
  auto discard = factory.BuildDiscardWorkspace("workspace-2", 4u, 13u, 100u);

  ASSERT_TRUE(save);
  ASSERT_TRUE(rename);
  ASSERT_TRUE(deletion);
  ASSERT_TRUE(discard);
  EXPECT_TRUE(IsValid(*save));
  EXPECT_TRUE(IsValid(*rename));
  EXPECT_TRUE(IsValid(*deletion));
  EXPECT_TRUE(IsValid(*discard));
}

TEST(CoreServiceCommandValidationWorkspaceTest,
     RefusesMalformedWorkspaceLifecycleFields) {
  CoreApiCommandFactory factory("profile", CreateCoreApiEntropySource());
  auto save = factory.BuildSaveWorkspace("workspace-1", 9u, 13u, 100u);
  auto rename = factory.BuildRenameWorkspace("workspace-1", 9u,
                                             "Evidence comparison", 13u, 100u);
  auto deletion = factory.BuildDeleteWorkspace(
      "workspace-1", 10u, std::string(64u, 'a'), 13u, 100u);
  auto discard = factory.BuildDiscardWorkspace("workspace-2", 4u, 13u, 100u);
  ASSERT_TRUE(save);
  ASSERT_TRUE(rename);
  ASSERT_TRUE(deletion);
  ASSERT_TRUE(discard);

  save->core_service_command->save_workspace->workspace_id.clear();
  rename->core_service_command->rename_workspace->display_name = " padded";
  deletion->core_service_command->delete_workspace->confirmation_token =
      std::string(64u, 'A');
  discard->core_service_command->discard_workspace->workspace_id.clear();
  EXPECT_FALSE(IsValid(*save));
  EXPECT_FALSE(IsValid(*rename));
  EXPECT_FALSE(IsValid(*deletion));
  EXPECT_FALSE(IsValid(*discard));
}

}  // namespace
}  // namespace taffy
