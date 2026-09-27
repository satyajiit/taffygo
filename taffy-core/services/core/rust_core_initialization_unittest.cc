// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "base/test/mock_log.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::CoreBootstrapPtr ValidBootstrap(uint64_t generation) {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = generation;
  bootstrap->generation_capability_entropy.resize(32u);
  for (size_t index = 0; index < 32u; ++index) {
    bootstrap->generation_capability_entropy[index] =
        static_cast<uint8_t>(index + 1u);
  }
  bootstrap->browser_profile_id = "profile-1";
  bootstrap->browser_session_id = "browser-session-1";
  bootstrap->available_account_methods = {
      mojom::AccountAuthMethod::kGoogle, mojom::AccountAuthMethod::kEmailLink,
      mojom::AccountAuthMethod::kGithub, mojom::AccountAuthMethod::kFacebook};
  return bootstrap;
}

mojom::CoreBootstrapPtr BootstrapWithInvalidTaskRestore(uint64_t generation) {
  auto bootstrap = ValidBootstrap(generation);
  auto task = mojom::TaskRestoreRecord::New();
  task->task_id = "task-1";
  task->task_id_seed.resize(32u);
  for (size_t index = 0; index < 32u; ++index) {
    task->task_id_seed[index] = static_cast<uint8_t>(index + 1u);
  }
  auto batch = mojom::CommittedTaskBatch::New();
  batch->effect_id = "task-effect-1";
  batch->expected_revision = 0u;
  batch->resulting_revision = 1u;
  // A valid batch begins with this header but cannot end there. Keeping the
  // native record structurally valid makes the Rust task decoder own refusal.
  batch->transaction_batch = {'T', 'A', 'F', 'F', 'Y', 'T', 'X', 'N', 0u};
  task->batches.push_back(std::move(batch));
  bootstrap->tasks.push_back(std::move(task));
  return bootstrap;
}

mojom::CoreBootstrapPtr BootstrapWithInvalidWorkspaceRestore(
    uint64_t generation) {
  auto bootstrap = ValidBootstrap(generation);
  auto workspace = mojom::WorkspaceRestoreRecord::New();
  workspace->workspace_id = "workspace-1";
  workspace->revision = 1u;
  // This reaches the typed Rust workspace restore but is not an encoded
  // storage-domain snapshot.
  workspace->snapshot = {0xffu};
  bootstrap->workspaces.push_back(std::move(workspace));
  return bootstrap;
}

void ExpectRestoreRefusalResetsBridge(
    mojom::CoreBootstrapPtr invalid_bootstrap,
    uint64_t retry_generation,
    const char* expected_refusal_log) {
  RustCore core;
  base::test::MockLog log;
  // The bridge also traces which task it refused, at WARNING, before this
  // line is written (decision 0235). The assertion is about the refusal line,
  // so every other line is let through rather than failed as unexpected.
  EXPECT_CALL(log, Log(testing::_, testing::_, testing::_, testing::_,
                       testing::_))
      .Times(testing::AnyNumber());
  EXPECT_CALL(log,
              Log(logging::LOGGING_ERROR, testing::_, testing::_, testing::_,
                  testing::HasSubstr(expected_refusal_log)))
      .WillOnce(testing::Return(true));
  log.StartCapturingLogs();

  CoreInitializationBatch refused =
      core.Initialize(std::move(invalid_bootstrap));

  log.StopCapturingLogs();
  ASSERT_TRUE(refused.result);
  EXPECT_EQ(mojom::InitializationStatus::kInvalidBootstrap,
            refused.result->status);
  EXPECT_EQ(0u, refused.result->accepted_generation);
  EXPECT_TRUE(refused.states.empty());

  CoreInitializationBatch retried =
      core.Initialize(ValidBootstrap(retry_generation));
  ASSERT_TRUE(retried.result);
  EXPECT_EQ(mojom::InitializationStatus::kReady, retried.result->status);
  EXPECT_EQ(retry_generation, retried.result->accepted_generation);
  ASSERT_EQ(1u, retried.states.size());
  ASSERT_TRUE(retried.states.front().state);
  EXPECT_EQ(retry_generation,
            retried.states.front().state->service_generation);
  EXPECT_EQ(1u, retried.states.front().state->sequence);
}

TEST(RustCoreInitializationTest,
     TaskRestoreRefusalResetsBridgeForValidRetry) {
  ExpectRestoreRefusalResetsBridge(
      BootstrapWithInvalidTaskRestore(/*generation=*/11u),
      /*retry_generation=*/12u,
      // The label names the refusal rather than being empty (decision 0235).
      "[taffy_core_initialization_refused] reason=task-restore "
      "label=restore_decode_codec");
}

TEST(RustCoreInitializationTest,
     WorkspaceRestoreRefusalResetsBridgeForValidRetry) {
  ExpectRestoreRefusalResetsBridge(
      BootstrapWithInvalidWorkspaceRestore(/*generation=*/13u),
      /*retry_generation=*/14u,
      "[taffy_core_initialization_refused] reason=workspace-restore");
}

}  // namespace
}  // namespace taffy
