// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class TestAppendCallback final : public TaskJournalAppendCallback {
 public:
  explicit TestAppendCallback(base::OnceCallback<void(bool)> callback)
      : callback_(std::move(callback)) {}

  void Run(bool committed) override {
    CHECK(callback_);
    std::move(callback_).Run(committed);
  }

 private:
  base::OnceCallback<void(bool)> callback_;
};

// Whether each command names a document, stated here as a literal rather than
// read from the predicate under test.
//
// The round-trip test below used to build its record from
// `BrowserCommandRequiresDocument` itself, so it agreed with that function
// whatever it answered — and it answered that a reload names a document, which
// no reload ever does. A test that reads its expectation from the thing it is
// testing cannot fail. This table is the second opinion.
constexpr std::array<std::pair<BrowserCommandType, bool>, 14>
    kCommandNamesADocument = {{
        {BrowserCommandType::kNavigate, false},
        {BrowserCommandType::kOpenTaskTab, false},
        {BrowserCommandType::kSearch, false},
        {BrowserCommandType::kGoBack, false},
        {BrowserCommandType::kGoForward, false},
        {BrowserCommandType::kOpenObservedLink, true},
        {BrowserCommandType::kListTaskTabs, true},
        {BrowserCommandType::kActivateTaskTab, true},
        {BrowserCommandType::kCloseTaskTab, true},
        {BrowserCommandType::kStartDownload, true},
        {BrowserCommandType::kListDownloads, true},
        {BrowserCommandType::kCancelDownload, true},
        {BrowserCommandType::kReload, false},
        {BrowserCommandType::kStopLoading, false},
    }};
static_assert(kCommandNamesADocument.size() == kAllBrowserCommandTypes.size(),
              "every closed command is stated here");

std::unique_ptr<TaskJournalAppendCallback> Completion(
    base::OnceCallback<void(bool)> callback) {
  return std::make_unique<TestAppendCallback>(std::move(callback));
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
  record.origin.kind = OriginKind::kTuple;
  record.origin.serialization = "https://example.test";
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
  result.observed_page_epoch = PageEpoch{"epoch-1"};
  result.observed_graph_revision = 7u;
  result.completed_at_monotonic_ms = 84u;
  result.repeat_may_duplicate_effect = false;
  result.failed_precondition = PreconditionKind::kExpectedDestination;
  return result;
}

bool AppendIntent(CoreStorageBroker* broker, DispatchIntentRecord record) {
  base::RunLoop loop;
  std::optional<bool> answer;
  bool call_returned = false;
  int completions = 0;
  broker->RecordDispatching(
      std::move(record),
      Completion(base::BindLambdaForTesting([&](bool committed) {
        EXPECT_TRUE(call_returned);
        ++completions;
        answer = committed;
        loop.Quit();
      })));
  call_returned = true;
  loop.Run();
  EXPECT_EQ(1, completions);
  return answer.value_or(false);
}

bool AppendTerminal(CoreStorageBroker* broker, ActionResult result) {
  base::RunLoop loop;
  std::optional<bool> answer;
  bool call_returned = false;
  int completions = 0;
  broker->RecordTerminalResult(
      std::move(result),
      Completion(base::BindLambdaForTesting([&](bool committed) {
        EXPECT_TRUE(call_returned);
        ++completions;
        answer = committed;
        loop.Quit();
      })));
  call_returned = true;
  loop.Run();
  EXPECT_EQ(1, completions);
  return answer.value_or(false);
}

class CoreStorageTaskActionJournalTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CoreStorageTaskActionJournalTest,
       DuplicateDispatchIdentityIsRefusedAcrossRestart) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    EXPECT_TRUE(AppendIntent(&broker, Intent()));
    EXPECT_FALSE(AppendIntent(&broker, Intent()));
    DispatchIntentRecord different = Intent();
    different.action_id = ActionId{"different-action"};
    EXPECT_FALSE(AppendIntent(&broker, std::move(different)));
  }
  task_environment_.RunUntilIdle();
  CoreStorageBroker restarted(path, false);
  EXPECT_FALSE(AppendIntent(&restarted, Intent()));
  DispatchIntentRecord different = Intent();
  different.task_id = TaskId{"different-task"};
  EXPECT_FALSE(AppendIntent(&restarted, std::move(different)));
}

TEST_F(CoreStorageTaskActionJournalTest,
       TerminalIsExactlyIdempotentAndMismatchIsRefused) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_TRUE(AppendIntent(&broker, Intent()));
    EXPECT_TRUE(AppendTerminal(&broker, Terminal()));
    EXPECT_TRUE(AppendTerminal(&broker, Terminal()));
    ActionResult mismatch = Terminal();
    mismatch.result_code = ActionResultCode::kOutcomeUnknown;
    EXPECT_FALSE(AppendTerminal(&broker, std::move(mismatch)));
  }
  task_environment_.RunUntilIdle();
  CoreStorageBroker restarted(path, false);
  EXPECT_TRUE(AppendTerminal(&restarted, Terminal()));
  ActionResult mismatch = Terminal();
  mismatch.completed_at_monotonic_ms++;
  EXPECT_FALSE(AppendTerminal(&restarted, std::move(mismatch)));
}

TEST_F(CoreStorageTaskActionJournalTest,
       IntentWithoutTerminalRemainsAmbiguousAfterRestart) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_TRUE(AppendIntent(&broker, Intent()));
  }
  task_environment_.RunUntilIdle();
  {
    CoreStorageBroker restarted(path, false);
    EXPECT_FALSE(AppendIntent(&restarted, Intent()));
  }
  task_environment_.RunUntilIdle();
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  sql::Statement row(database.GetUniqueStatement(
      "SELECT result_code FROM core_task_action_journal "
      "WHERE dispatch_id='dispatch-1'"));
  ASSERT_TRUE(row.Step());
  EXPECT_EQ(sql::ColumnType::kNull, row.GetColumnType(0));
}

TEST_F(CoreStorageTaskActionJournalTest,
       EveryClosedBrowserCommandRoundTripsThroughTheDurableJournal) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    for (size_t index = 0; index < kCommandNamesADocument.size(); ++index) {
      const auto& [command, names_a_document] = kCommandNamesADocument[index];
      EXPECT_EQ(kAllBrowserCommandTypes[index], command) << index;
      DispatchIntentRecord record = Intent();
      record.dispatch_id =
          DispatchId{"dispatch-command-" + std::to_string(index)};
      record.action_id = ActionId{"action-command-" + std::to_string(index)};
      record.action_type.reset();
      record.command_type = command;
      if (!names_a_document) {
        record.frame_id = FrameId{};
        record.page_epoch = PageEpoch{};
        record.graph_revision = 0u;
        record.origin = Origin{};
      }
      EXPECT_TRUE(AppendIntent(&broker, std::move(record))) << index;
    }
  }
  task_environment_.RunUntilIdle();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  sql::Statement rows(database.GetUniqueStatement(
      "SELECT command_type FROM core_task_action_journal ORDER BY sequence"));
  size_t index = 0u;
  while (rows.Step()) {
    ASSERT_LT(index, kAllBrowserCommandTypes.size());
    EXPECT_EQ(static_cast<int>(kAllBrowserCommandTypes[index]),
              rows.ColumnInt(0));
    ++index;
  }
  EXPECT_EQ(kAllBrowserCommandTypes.size(), index);
  EXPECT_EQ(kMaxBrowserCommandTypeWireValue,
            static_cast<uint8_t>(kAllBrowserCommandTypes.back()));
}

TEST(BrowserCommandDocumentTest, EveryCommandSaysWhetherItNamesADocument) {
  for (const auto& [command, names_a_document] : kCommandNamesADocument) {
    EXPECT_EQ(BrowserCommandRequiresDocument(command), names_a_document)
        << "command " << static_cast<int>(command);
  }
}

// The shape a reload actually arrives in. `AuthorizedBrowserCommand` sets
// `source_handle` for `kOpenObservedLink` alone, so the dispatcher builds this
// record with no document at all; a journal that demanded one refused the
// append, and the dispatch came back `kDispatchFailed`.
TEST_F(CoreStorageTaskActionJournalTest, ALifecycleControlJournalsNoDocument) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  CoreStorageBroker broker(path, false);
  for (BrowserCommandType command :
       {BrowserCommandType::kReload, BrowserCommandType::kStopLoading}) {
    DispatchIntentRecord record = Intent();
    record.dispatch_id =
        DispatchId{"dispatch-lifecycle-" +
                   std::to_string(static_cast<int>(command))};
    record.action_id =
        ActionId{"action-lifecycle-" + std::to_string(static_cast<int>(command))};
    record.action_type.reset();
    record.command_type = command;
    record.frame_id = FrameId{};
    record.page_epoch = PageEpoch{};
    record.graph_revision = 0u;
    record.origin = Origin{};
    EXPECT_TRUE(AppendIntent(&broker, std::move(record)))
        << static_cast<int>(command);
  }
}

TEST_F(CoreStorageTaskActionJournalTest, PrivateProfileCreatesNoFile) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path =
      directory.GetPath().AppendASCII("must-not-exist.sqlite3");
  {
    CoreStorageBroker private_broker(path, true);
    EXPECT_TRUE(AppendIntent(&private_broker, Intent()));
    EXPECT_TRUE(AppendTerminal(&private_broker, Terminal()));
    EXPECT_FALSE(base::PathExists(path));
  }
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(path));
}

TEST_F(CoreStorageTaskActionJournalTest, StorageEdgeRejectsInvalidShapes) {
  CoreStorageBroker broker(base::FilePath(), true);
  DispatchIntentRecord invalid = Intent();
  invalid.dispatch_id.value.assign(kMaxIdentifierChars + 1u, 'x');
  EXPECT_FALSE(AppendIntent(&broker, std::move(invalid)));
  invalid = Intent();
  invalid.command_type = BrowserCommandType::kNavigate;
  EXPECT_FALSE(AppendIntent(&broker, std::move(invalid)));
  invalid = Intent();
  invalid.action_type = static_cast<ActionType>(255);
  EXPECT_FALSE(AppendIntent(&broker, std::move(invalid)));
  invalid = Intent();
  invalid.origin.serialization = "https://example.test/private?q=secret";
  EXPECT_FALSE(AppendIntent(&broker, std::move(invalid)));
  invalid = Intent();
  invalid.recorded_at_monotonic_ms = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(AppendIntent(&broker, std::move(invalid)));

  ASSERT_TRUE(AppendIntent(&broker, Intent()));
  ActionResult result = Terminal();
  result.result_code = static_cast<ActionResultCode>(255);
  EXPECT_FALSE(AppendTerminal(&broker, std::move(result)));
  result = Terminal();
  result.observed_graph_revision.reset();
  EXPECT_FALSE(AppendTerminal(&broker, std::move(result)));
  result = Terminal();
  result.terminal = false;
  EXPECT_FALSE(AppendTerminal(&broker, std::move(result)));
}

TEST_F(CoreStorageTaskActionJournalTest,
       ExactRecordShapeStoresNoPageOrOperandContent) {
  constexpr char kDetailCanary[] = "form-value-canary-7391";
  constexpr char kAddressCanary[] =
      "https://forbidden.test/private?search=query-canary-2846";
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_TRUE(AppendIntent(&broker, Intent()));
    ActionResult result = Terminal();
    result.detail_code = kDetailCanary;
    PostconditionOutcome outcome;
    outcome.postcondition.kind = PostconditionKind::kCommittedNavigation;
    Destination destination;
    destination.url_metadata.disclosure = UrlDisclosure::kFullUrl;
    destination.url_metadata.url = kAddressCanary;
    outcome.postcondition.expected_destination = std::move(destination);
    result.postcondition_outcomes.push_back(std::move(outcome));
    ASSERT_TRUE(AppendTerminal(&broker, std::move(result)));
  }
  task_environment_.RunUntilIdle();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  {
    std::vector<std::string> columns;
    sql::Statement info(database.GetUniqueStatement(
        "PRAGMA table_info(core_task_action_journal)"));
    while (info.Step()) {
      columns.push_back(info.ColumnString(1));
    }
    EXPECT_EQ((std::vector<std::string>{"sequence",
                                        "dispatch_id",
                                        "task_id",
                                        "action_id",
                                        "capability_reference",
                                        "actor_lease_id",
                                        "tab_id",
                                        "action_type",
                                        "command_type",
                                        "frame_id",
                                        "page_epoch",
                                        "graph_revision",
                                        "origin_kind",
                                        "origin_value",
                                        "idempotency_policy",
                                        "recorded_at_monotonic_ms",
                                        "request_id",
                                        "result_code",
                                        "dispatched",
                                        "completed_at_monotonic_ms",
                                        "repeat_may_duplicate_effect",
                                        "observed_page_epoch",
                                        "observed_graph_revision",
                                        "failed_precondition"}),
              columns);
    sql::Statement row(database.GetUniqueStatement(
        "SELECT dispatch_id,origin_value,result_code,observed_graph_revision "
        "FROM core_task_action_journal"));
    ASSERT_TRUE(row.Step());
    EXPECT_EQ("dispatch-1", row.ColumnString(0));
    EXPECT_EQ("https://example.test", row.ColumnString(1));
    EXPECT_EQ(static_cast<int>(ActionResultCode::kPostconditionFailed),
              row.ColumnInt(2));
    EXPECT_EQ(7, row.ColumnInt64(3));
  }
  database.Close();

  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(path, &bytes));
  EXPECT_EQ(std::string::npos, bytes.find(kDetailCanary));
  EXPECT_EQ(std::string::npos, bytes.find(kAddressCanary));
}

}  // namespace
}  // namespace taffy
