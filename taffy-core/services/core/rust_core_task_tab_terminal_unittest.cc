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

mojom::TaskEffectBindingPtr TaskTabListBinding() {
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
      mojom::PolicyActionClass::kObservePage;
  binding->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kTabsList;
  binding->action->executable->task_tab =
      mojom::TaskTabActionBinding::New();
  binding->action->executable->task_tab->browser_session_id =
      "browser-session-1";
  return binding;
}

mojom::TaskEffectCompletionPtr TaskTabListCompletion(
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
  completion->effect_result->browser_action->outcome =
      mojom::BrowserActionOutcome::kCompleted;
  completion->effect_result->browser_action->dispatch_id =
      binding.action->dispatch_id;
  completion->effect_result->browser_action->task_tab =
      mojom::TaskTabActionResult::New();
  auto& result = completion->effect_result->browser_action->task_tab;
  result->browser_session_id = "browser-session-1";
  result->operation_kind = mojom::TaskActionOperationKind::kTabsList;
  result->postcondition = mojom::TaskTabPostcondition::kListed;
  result->state_was_already_satisfied = false;
  return completion;
}

TEST(RustCoreTaskTabTerminalTest, ExactTypedListResultCrossesTheServiceSeam) {
  const mojom::TaskEffectBindingPtr binding = TaskTabListBinding();
  const mojom::TaskEffectCompletionPtr completion =
      TaskTabListCompletion(*binding);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_TRUE(terminal->has_task_tab_result);
  EXPECT_EQ("browser-session-1",
            std::string(terminal->task_tab_browser_session_id));
  EXPECT_TRUE(terminal->task_tab_snapshots.empty());
  EXPECT_FALSE(terminal->task_tab_state_was_already_satisfied);
}

TEST(RustCoreTaskTabTerminalTest,
     DiscoverySessionFieldCannotMasqueradeAsATaskTabResult) {
  const mojom::TaskEffectBindingPtr binding = TaskTabListBinding();
  mojom::TaskEffectCompletionPtr completion = TaskTabListCompletion(*binding);
  completion->effect_result->browser_action->browser_session_id =
      "browser-session-1";

  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
}

}  // namespace
}  // namespace taffy
