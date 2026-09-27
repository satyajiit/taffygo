// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A sheet the browser will not draw is still answered. The bridge treats a
// field-value terminal as the sheet having reached its owner, whatever its
// status, and the task then waits for the person's count; refused before any
// sheet existed, that count could never come, and on a phone an errand sat
// under "Taffy needs something" with nothing to fill in.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 1u;
constexpr uint64_t kRevision = 4u;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

mojom::CoreStateBrowserBindingsPtr Bindings() {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  bindings->task_revisions.push_back(
      mojom::TaskRevisionBinding::New("task-1", kGeneration, kRevision,
                                      std::vector<mojom::TaskControlKind>()));
  return bindings;
}

mojom::CoreStateUpdatePtr State() {
  auto state = mojom::CoreStateUpdate::New();
  state->service_generation = kGeneration;
  state->sequence = 1u;
  state->core_status_schema_version = 1u;
  state->payload = std::vector<uint8_t>{1u};
  return state;
}

// An ask naming a tab this browser holds no document for, which is the first
// of the two refusals, and the one a test can reach without a live page.
mojom::TaskEffectBindingPtr AskInATabWithNoDocument() {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New(
      "field-operation", kGeneration, kRevision, NowMonotonicMillis() + 30'000u,
      "field-idempotency");
  effect->effect_id = "field-effect";
  effect->task_id = "task-1";
  effect->ordinal = 0u;
  effect->kind = mojom::TaskReducerEffectKind::kRequestFieldValues;
  effect->field_values = mojom::TaskFieldValuesEffect::New(
      "field-request-1", "tab-nobody-holds", "node-1",
      std::vector<std::string>());
  return effect;
}

class CoreServiceManagerRefusedFieldSheetTest : public testing::Test {
 protected:
  void SetUp() override {
    auto tools = std::make_unique<ProfileToolSupervisor>(
        kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    manager_ = tail_.MakeManager(
        &context_, /*storage_broker=*/nullptr, std::move(tools),
        base::MakeRefCounted<CorePageObservationBroker>(&context_),
        std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
    service_ = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
    ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                  *manager_, Bindings()),
              mojom::PendingApprovalRegistrationStatus::kRegistered);
    // The task's revision is read from the published state, as it is on a
    // phone, where a task that asks is always one the core has published.
    CoreServiceManagerTaskEffectTestPeer::Publish(*manager_, State());
    ASSERT_EQ(
        CoreServiceManagerTaskEffectTestPeer::PendingAdmissionCount(*manager_),
        0u);
  }

  void TearDown() override { manager_->Shutdown(); }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::TaffyCoreService> service_;
  mojo::PendingReceiver<mojom::CoreSession> session_;
};

TEST_F(CoreServiceManagerRefusedFieldSheetTest,
       ARefusedSheetIsAnsweredWithNothingSuppliedAndAMoveToMake) {
  std::optional<mojom::TaskEffectCompletionStatus> status;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager_, AskInATabWithNoDocument(),
      base::BindLambdaForTesting(
          [&status](mojom::TaskEffectCompletionPtr completion) {
            ASSERT_TRUE(completion);
            status = completion->status;
          }));

  // The effect is still refused: nothing drew a sheet.
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(*status, mojom::TaskEffectCompletionStatus::kRefused);

  // And the ask is answered, so the task is not left waiting on it.
  const mojom::CoreServiceCommand* answer =
      CoreServiceManagerTaskEffectTestPeer::OnlyPendingCommand(*manager_);
  ASSERT_NE(answer, nullptr);
  EXPECT_EQ(answer->kind, mojom::CoreServiceCommandKind::kSupplyFieldValues);
  ASSERT_TRUE(answer->supply_field_values);
  EXPECT_EQ(answer->supply_field_values->task_id, "task-1");
  EXPECT_EQ(answer->supply_field_values->request_id, "field-request-1");
  EXPECT_EQ(answer->supply_field_values->supplied, 0u);
  EXPECT_EQ(answer->supply_field_values->outcome,
            mojom::FieldValueAskOutcome::kCannotBeShown);
  // Entered only for the submission that answered it: a later ask with the
  // same identity is not mistaken for one already handed to an owner.
  EXPECT_EQ(CoreServiceManagerTaskEffectTestPeer::EmittedFieldValueRequestCount(
                *manager_),
            0u);
}

}  // namespace
}  // namespace taffy
