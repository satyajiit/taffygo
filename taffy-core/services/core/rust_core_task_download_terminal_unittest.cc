// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

mojom::TaskEffectBindingPtr DownloadBinding(
    mojom::TaskActionOperationKind operation) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "operation-1", 1u, 7u, 10'000u, "operation-key-1");
  binding->effect_id = "effect-1";
  binding->task_id = "task-1";
  binding->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  binding->action = mojom::TaskActionEffect::New();
  binding->action->dispatch_id = "dispatch-1";
  binding->action->executable = mojom::TaskExecutableAction::New();
  binding->action->executable->action_class =
      operation != mojom::TaskActionOperationKind::kDownloadList
          ? mojom::PolicyActionClass::kStartDownload
          : mojom::PolicyActionClass::kObservePage;
  binding->action->executable->operation_kind = operation;
  binding->action->executable->task_download =
      mojom::TaskDownloadActionBinding::New();
  binding->action->executable->task_download->browser_session_id =
      "browser-session-1";
  return binding;
}

mojom::TaskDownloadSnapshotPtr DownloadSnapshot(std::string id) {
  return mojom::TaskDownloadSnapshot::New(
      std::move(id), mojom::TaskDownloadState::kComplete,
      mojom::TaskDownloadMediaType::kApplication, 42u,
      mojom::TaskDownloadDirectoryClass::kUndecided);
}

mojom::TaskEffectCompletionPtr DownloadCompletion(
    const mojom::TaskEffectBinding& binding) {
  auto completion = mojom::TaskEffectCompletion::New();
  completion->operation = binding.operation->Clone();
  completion->effect_id = binding.effect_id;
  completion->task_id = binding.task_id;
  completion->kind = binding.kind;
  completion->status = mojom::TaskEffectCompletionStatus::kSucceeded;
  completion->effect_result = mojom::EffectResult::New();
  completion->effect_result->operation = binding.operation->Clone();
  completion->effect_result->effect_id = binding.effect_id;
  completion->effect_result->status = mojom::EffectStatus::kCompleted;
  completion->effect_result->kind = mojom::EffectKind::kBrowserAction;
  completion->effect_result->browser_action =
      mojom::BrowserActionEffectResult::New();
  auto& browser_action = completion->effect_result->browser_action;
  browser_action->outcome = mojom::BrowserActionOutcome::kCompleted;
  browser_action->dispatch_id = binding.action->dispatch_id;
  browser_action->task_download = mojom::TaskDownloadActionResult::New();
  auto& result = browser_action->task_download;
  result->browser_session_id = "browser-session-1";
  result->operation_kind = binding.action->executable->operation_kind;
  result->postcondition =
      result->operation_kind == mojom::TaskActionOperationKind::kDownloadStart
          ? mojom::TaskDownloadPostcondition::kStarted
          : (result->operation_kind ==
                     mojom::TaskActionOperationKind::kDownloadList
                 ? mojom::TaskDownloadPostcondition::kListed
                 : mojom::TaskDownloadPostcondition::kCancelled);
  return completion;
}

TEST(RustCoreTaskDownloadTerminalTest,
     ExactContentFreeListResultCrossesTheServiceSeam) {
  const mojom::TaskEffectBindingPtr binding =
      DownloadBinding(mojom::TaskActionOperationKind::kDownloadList);
  mojom::TaskEffectCompletionPtr completion = DownloadCompletion(*binding);
  completion->effect_result->browser_action->task_download->downloads.push_back(
      DownloadSnapshot("opaque-guid-1"));

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_TRUE(terminal->has_task_download_result);
  EXPECT_EQ("browser-session-1",
            std::string(terminal->task_download_browser_session_id));
  ASSERT_EQ(1u, terminal->task_download_snapshots.size());
  EXPECT_EQ("opaque-guid-1",
            std::string(terminal->task_download_snapshots[0].download_id));
  EXPECT_EQ(42u, terminal->task_download_snapshots[0].received_bytes);
  EXPECT_FALSE(terminal->task_download_truncated);
  EXPECT_FALSE(terminal->has_task_tab_result);
  EXPECT_FALSE(terminal->has_observation);
}

TEST(RustCoreTaskDownloadTerminalTest,
     StartRequiresOneExactUntruncatedManagerWitness) {
  const mojom::TaskEffectBindingPtr binding =
      DownloadBinding(mojom::TaskActionOperationKind::kDownloadStart);
  mojom::TaskEffectCompletionPtr completion = DownloadCompletion(*binding);

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));

  completion->effect_result->browser_action->task_download->downloads.push_back(
      DownloadSnapshot("opaque-guid-1"));
  EXPECT_TRUE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));

  completion->effect_result->browser_action->task_download->truncated = true;
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
}

TEST(RustCoreTaskDownloadTerminalTest,
     WrongSessionDuplicateIdentityOrFalseTruncationIsRefused) {
  const mojom::TaskEffectBindingPtr binding =
      DownloadBinding(mojom::TaskActionOperationKind::kDownloadList);
  mojom::TaskEffectCompletionPtr completion = DownloadCompletion(*binding);
  auto& result = completion->effect_result->browser_action->task_download;
  result->downloads.push_back(DownloadSnapshot("opaque-guid-1"));

  result->browser_session_id = "browser-session-other";
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
  result->browser_session_id = "browser-session-1";

  result->downloads.push_back(DownloadSnapshot("opaque-guid-1"));
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
  result->downloads.pop_back();

  result->truncated = true;
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
}

TEST(RustCoreTaskDownloadTerminalTest,
     AnotherResultFamilyCannotMasqueradeAsADownload) {
  const mojom::TaskEffectBindingPtr binding =
      DownloadBinding(mojom::TaskActionOperationKind::kDownloadList);
  mojom::TaskEffectCompletionPtr completion = DownloadCompletion(*binding);
  completion->effect_result->browser_action->task_tab =
      mojom::TaskTabActionResult::New();

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
}

TEST(RustCoreTaskDownloadTerminalTest,
     CancelRequiresTheExactBindingAndCancelledWitness) {
  mojom::TaskEffectBindingPtr binding =
      DownloadBinding(mojom::TaskActionOperationKind::kDownloadCancel);
  binding->action->executable->task_download->download_id = "opaque-guid-1";
  mojom::TaskEffectCompletionPtr completion = DownloadCompletion(*binding);
  auto snapshot = DownloadSnapshot("opaque-guid-1");
  snapshot->state = mojom::TaskDownloadState::kCancelled;
  completion->effect_result->browser_action->task_download->downloads.push_back(
      std::move(snapshot));

  EXPECT_TRUE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
  completion->effect_result->browser_action->task_download->downloads[0]
      ->download_id = "opaque-guid-other";
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
}

}  // namespace
}  // namespace taffy
