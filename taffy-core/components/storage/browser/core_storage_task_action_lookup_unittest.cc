// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class AppendCallback final : public TaskJournalAppendCallback {
 public:
  explicit AppendCallback(base::OnceCallback<void(bool)> callback)
      : callback_(std::move(callback)) {}

  void Run(bool committed) override {
    CHECK(callback_);
    std::move(callback_).Run(committed);
  }

 private:
  base::OnceCallback<void(bool)> callback_;
};

std::unique_ptr<TaskJournalAppendCallback> Completion(
    base::OnceCallback<void(bool)> callback) {
  return std::make_unique<AppendCallback>(std::move(callback));
}

DispatchIntentRecord Intent() {
  DispatchIntentRecord record;
  record.dispatch_id = DispatchId{"dispatch-1"};
  record.task_id = TaskId{"task-1"};
  record.action_id = ActionId{"action-1"};
  record.action_type = ActionType::kActivate;
  record.capability_reference = CapabilityReference{"capability-1"};
  record.actor_lease_id = ActorLeaseId{"lease-1"};
  record.tab_id = TabId{"tab-1"};
  record.frame_id = FrameId{"frame-1"};
  record.page_epoch = PageEpoch{"epoch-1"};
  record.graph_revision = 7u;
  record.origin = Origin{
      .kind = OriginKind::kTuple,
      .serialization = "https://example.test",
  };
  record.idempotency_policy = IdempotencyPolicy::kIdempotentWrite;
  record.recorded_at_monotonic_ms = 42u;
  return record;
}

ActionResult Terminal() {
  ActionResult result;
  result.schema_version = "0.9";
  result.request_id = RequestId{"request-1"};
  result.action_id = ActionId{"action-1"};
  result.dispatch_id = DispatchId{"dispatch-1"};
  result.result_code = ActionResultCode::kPostconditionFailed;
  result.dispatched = true;
  result.terminal = true;
  result.completed_at_monotonic_ms = 84u;
  result.repeat_may_duplicate_effect = false;
  return result;
}

bool AppendIntent(CoreStorageBroker* broker) {
  base::RunLoop loop;
  std::optional<bool> answer;
  broker->RecordDispatching(
      Intent(), Completion(base::BindLambdaForTesting([&](bool committed) {
        answer = committed;
        loop.Quit();
      })));
  loop.Run();
  return answer.value_or(false);
}

bool AppendTerminal(CoreStorageBroker* broker) {
  base::RunLoop loop;
  std::optional<bool> answer;
  broker->RecordTerminalResult(
      Terminal(), Completion(base::BindLambdaForTesting([&](bool committed) {
        answer = committed;
        loop.Quit();
      })));
  loop.Run();
  return answer.value_or(false);
}

std::optional<TaskActionJournalLookup> ReadJournal(
    CoreStorageBroker* broker,
    DispatchId dispatch_id = DispatchId{"dispatch-1"},
    TaskId task_id = TaskId{"task-1"},
    ActionId action_id = ActionId{"action-1"}) {
  base::RunLoop loop;
  std::optional<TaskActionJournalLookup> answer;
  broker->ReadTaskActionJournal(
      std::move(dispatch_id), std::move(task_id), std::move(action_id),
      base::BindLambdaForTesting(
          [&](std::optional<TaskActionJournalLookup> lookup) {
            answer = lookup;
            loop.Quit();
          }));
  loop.Run();
  return answer;
}

class CoreStorageTaskActionLookupTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CoreStorageTaskActionLookupTest,
       ExactReadDistinguishesMissingOpenAndTerminalRowsAcrossRestart) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    const std::optional<TaskActionJournalLookup> missing = ReadJournal(&broker);
    ASSERT_TRUE(missing);
    EXPECT_EQ(TaskActionJournalState::kMissing, missing->state);

    ASSERT_TRUE(AppendIntent(&broker));
    const std::optional<TaskActionJournalLookup> open = ReadJournal(&broker);
    ASSERT_TRUE(open);
    EXPECT_EQ(TaskActionJournalState::kIntentOnly, open->state);

    ASSERT_TRUE(AppendTerminal(&broker));
    const std::optional<TaskActionJournalLookup> terminal = ReadJournal(&broker);
    ASSERT_TRUE(terminal);
    EXPECT_EQ(TaskActionJournalState::kTerminal, terminal->state);
    EXPECT_EQ(ActionResultCode::kPostconditionFailed, terminal->result_code);

    const std::optional<TaskActionJournalLookup> wrong_task =
        ReadJournal(&broker, DispatchId{"dispatch-1"}, TaskId{"task-2"},
                    ActionId{"action-1"});
    ASSERT_TRUE(wrong_task);
    EXPECT_EQ(TaskActionJournalState::kMissing, wrong_task->state);
  }
  task_environment_.RunUntilIdle();

  CoreStorageBroker restarted(path, false);
  const std::optional<TaskActionJournalLookup> terminal =
      ReadJournal(&restarted);
  ASSERT_TRUE(terminal);
  EXPECT_EQ(TaskActionJournalState::kTerminal, terminal->state);
  EXPECT_EQ(ActionResultCode::kPostconditionFailed, terminal->result_code);
}

TEST_F(CoreStorageTaskActionLookupTest, InvalidJournalReadFailsClosed) {
  CoreStorageBroker broker(base::FilePath(), true);
  EXPECT_FALSE(ReadJournal(&broker, DispatchId{}, TaskId{"task-1"},
                           ActionId{"action-1"}));
}

}  // namespace
}  // namespace taffy
