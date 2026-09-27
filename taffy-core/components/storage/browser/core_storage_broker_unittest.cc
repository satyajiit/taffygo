// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_broker.h"

#include <string_view>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr char kWorkspaceId[] = "11111111111111111111111111111111";

void AppendU32(std::vector<uint8_t>* bytes, uint32_t value) {
  for (size_t index = 0u; index < 4u; ++index) {
    bytes->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void AppendU64(std::vector<uint8_t>* bytes, uint64_t value) {
  for (size_t index = 0u; index < 8u; ++index) {
    bytes->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void AppendString(std::vector<uint8_t>* bytes, std::string_view value) {
  AppendU32(bytes, static_cast<uint32_t>(value.size()));
  bytes->insert(bytes->end(), value.begin(), value.end());
}

std::vector<uint8_t> WorkspaceSnapshot(uint64_t revision,
                                       std::string_view goal = "test goal") {
  std::vector<uint8_t> bytes{'T', 'A', 'F', 'F', 'Y', 'W', 'S', '1'};
  AppendU32(&bytes, 1u);
  AppendString(&bytes, kWorkspaceId);
  AppendU64(&bytes, revision);
  AppendString(&bytes, goal);
  bytes.push_back(0u);
  AppendU64(&bytes, 1u);
  bytes.push_back(0u);
  AppendU32(&bytes, 0u);
  AppendU32(&bytes, 0u);
  return bytes;
}

mojom::EffectEnvelopePtr StorageEffect(std::string effect_id,
                                       uint64_t expected,
                                       uint64_t resulting,
                                       uint8_t seed_offset = 1) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = "operation-" + effect_id;
  effect->operation->service_generation = 1;
  effect->operation->task_revision = resulting;
  effect->operation->deadline_monotonic_ms = 10'000;
  effect->operation->idempotency_key = "key-" + effect_id;
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kAppendTaskCommit;
  effect->storage_commit->task_id = "task-1";
  effect->storage_commit->expected_revision = expected;
  effect->storage_commit->resulting_revision = resulting;
  effect->storage_commit->transaction_batch = {1, 2, 3};
  effect->storage_commit->task_id_seed.resize(32u);
  for (size_t index = 0; index < effect->storage_commit->task_id_seed.size();
       ++index) {
    effect->storage_commit->task_id_seed[index] =
        static_cast<uint8_t>(seed_offset + index);
  }
  return effect;
}

mojom::EffectEnvelopePtr AtomicWorkspaceEffect(std::string effect_id,
                                               uint64_t task_expected,
                                               uint64_t task_resulting,
                                               uint64_t workspace_expected,
                                               uint64_t workspace_resulting,
                                               std::vector<uint8_t> snapshot) {
  mojom::EffectEnvelopePtr effect =
      StorageEffect(std::move(effect_id), task_expected, task_resulting);
  effect->storage_commit->workspace = mojom::WorkspacePersistEffect::New();
  effect->storage_commit->workspace->workspace_id = kWorkspaceId;
  effect->storage_commit->workspace->expected_revision = workspace_expected;
  effect->storage_commit->workspace->resulting_revision = workspace_resulting;
  effect->storage_commit->workspace->snapshot = std::move(snapshot);
  return effect;
}

mojom::EffectResultPtr Dispatch(CoreStorageBroker* broker,
                                mojom::EffectEnvelopePtr effect) {
  base::RunLoop loop;
  mojom::EffectResultPtr result;
  broker->DispatchStorage(
      std::move(effect),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr terminal) {
        result = std::move(terminal);
        loop.Quit();
      }));
  loop.Run();
  return result;
}

mojom::CoreBootstrapPtr Load(CoreStorageBroker* broker) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      1, false, base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr loaded) {
        bootstrap = std::move(loaded);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

bool CommitIntent(CoreStorageBroker* broker,
                  const mojom::EffectEnvelope& effect) {
  base::RunLoop loop;
  bool committed = false;
  broker->CommitIntent(
      effect, base::BindLambdaForTesting([&](bool result) {
        committed = result;
        loop.Quit();
      }));
  loop.Run();
  return committed;
}

bool CommitResult(CoreStorageBroker* broker,
                  const mojom::EffectResult& result) {
  base::RunLoop loop;
  bool committed = false;
  broker->CommitResult(
      result, base::BindLambdaForTesting([&](bool value) {
        committed = value;
        loop.Quit();
      }));
  loop.Run();
  return committed;
}

mojom::EffectEnvelopePtr JournalEffect(std::string effect_id) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New(
      "operation-" + effect_id, 1u, 0u, 10'000u, "key-" + effect_id);
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kPageObservation;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  return effect;
}

mojom::EffectResultPtr JournalResult(const mojom::EffectEnvelope& effect) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = effect.kind;
  return result;
}

class CoreStorageBrokerTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(CoreStorageBrokerTest, CommitsAtExpectedRevisionAndReplaysIdempotently) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  CoreStorageBroker broker(path, false);

  mojom::EffectResultPtr first =
      Dispatch(&broker, StorageEffect("create", 0, 1));
  ASSERT_TRUE(first);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, first->status);
  EXPECT_EQ(1u, first->storage->committed_revision);

  mojom::EffectResultPtr duplicate =
      Dispatch(&broker, StorageEffect("create", 0, 1));
  ASSERT_TRUE(duplicate);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, duplicate->status);

  mojom::EffectResultPtr mismatched_seed =
      Dispatch(&broker, StorageEffect("create", 0, 1, 2));
  ASSERT_TRUE(mismatched_seed);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, mismatched_seed->status);

  mojom::EffectResultPtr stale =
      Dispatch(&broker, StorageEffect("stale", 0, 2));
  ASSERT_TRUE(stale);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, stale->status);

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_EQ(storage_schema::kVersion, bootstrap->core_journal_schema_version);
  EXPECT_EQ(storage_schema::kChecksum, bootstrap->core_journal_schema_checksum);
  EXPECT_FALSE(bootstrap->account_session);
  ASSERT_EQ(1u, bootstrap->tasks.size());
  ASSERT_EQ(1u, bootstrap->tasks[0]->batches.size());
  EXPECT_EQ("create", bootstrap->tasks[0]->batches[0]->effect_id);
  EXPECT_EQ(1u, bootstrap->tasks[0]->task_id_seed[0]);
  EXPECT_EQ(32u, bootstrap->tasks[0]->task_id_seed[31]);
}

TEST_F(CoreStorageBrokerTest,
       LostEffectsAreScopedToExactBrokerOwnedIdentities) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  mojom::EffectEnvelopePtr prior_session = JournalEffect("prior-session");
  mojom::EffectEnvelopePtr current_session = JournalEffect("current-session");
  ASSERT_TRUE(CommitIntent(&broker, *prior_session));
  ASSERT_TRUE(CommitIntent(&broker, *current_session));

  broker.MarkEffectsLost(1u, {current_session->effect_id});
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(CommitResult(&broker, *JournalResult(*prior_session)));
  EXPECT_FALSE(CommitResult(&broker, *JournalResult(*current_session)));
}

TEST_F(CoreStorageBrokerTest,
       TaskAndWorkspaceCommitAtomicallyReplayAndRestoreAfterRestart) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    mojom::EffectResultPtr first =
        Dispatch(&broker, AtomicWorkspaceEffect("atomic-create", 0, 1, 0, 1,
                                                WorkspaceSnapshot(1u)));
    ASSERT_TRUE(first);
    EXPECT_EQ(mojom::EffectStatus::kCompleted, first->status);
    EXPECT_EQ(1u, first->storage->committed_revision);

    mojom::EffectResultPtr duplicate =
        Dispatch(&broker, AtomicWorkspaceEffect("atomic-create", 0, 1, 0, 1,
                                                WorkspaceSnapshot(1u)));
    ASSERT_TRUE(duplicate);
    EXPECT_EQ(mojom::EffectStatus::kCompleted, duplicate->status);

    mojom::EffectResultPtr mismatched_replay = Dispatch(
        &broker, AtomicWorkspaceEffect("atomic-create", 0, 1, 0, 1,
                                       WorkspaceSnapshot(1u, "changed")));
    ASSERT_TRUE(mismatched_replay);
    EXPECT_EQ(mojom::EffectStatus::kUnavailable, mismatched_replay->status);

    // The task half is valid, but the workspace half is stale. Neither may
    // land: this is the crash window the combined transaction closes.
    mojom::EffectResultPtr stale = Dispatch(
        &broker, AtomicWorkspaceEffect("atomic-stale", 1, 2, 0, 1,
                                       WorkspaceSnapshot(1u, "stale")));
    ASSERT_TRUE(stale);
    EXPECT_EQ(mojom::EffectStatus::kUnavailable, stale->status);
  }
  task_environment_.RunUntilIdle();

  CoreStorageBroker restarted(path, false);
  mojom::CoreBootstrapPtr bootstrap = Load(&restarted);
  ASSERT_TRUE(bootstrap);
  ASSERT_EQ(1u, bootstrap->tasks.size());
  EXPECT_EQ(1u, bootstrap->tasks.front()->batches.size());
  ASSERT_EQ(1u, bootstrap->workspaces.size());
  EXPECT_EQ(kWorkspaceId, bootstrap->workspaces.front()->workspace_id);
  EXPECT_EQ(1u, bootstrap->workspaces.front()->revision);
  EXPECT_EQ(WorkspaceSnapshot(1u), bootstrap->workspaces.front()->snapshot);
}

TEST_F(CoreStorageBrokerTest, RefusesDegenerateTaskSeed) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);

  mojom::EffectEnvelopePtr effect = StorageEffect("create", 0, 1);
  effect->storage_commit->task_id_seed.assign(32u, 7u);
  mojom::EffectResultPtr result = Dispatch(&broker, std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, result->status);
}

TEST_F(CoreStorageBrokerTest, RefusesARecordedSchemaChecksumMismatch) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  ASSERT_TRUE(database.Execute(storage_schema::kLedgerStatement));
  {
    sql::Statement insert(database.GetUniqueStatement(
        "INSERT INTO taffy_storage_schema(component,version,checksum) "
        "VALUES(?,?,?)"));
    insert.BindString(0, storage_schema::kComponent);
    insert.BindInt(1, static_cast<int>(storage_schema::kVersion));
    insert.BindString(2, "wrong-checksum");
    ASSERT_TRUE(insert.Run());
  }
  database.Close();

  CoreStorageBroker broker(path, false);
  EXPECT_FALSE(Load(&broker));
}

TEST_F(CoreStorageBrokerTest, PrivateProfileNeverCreatesAFile) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("must-not-exist");
  CoreStorageBroker broker(base::FilePath(), true);

  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker.LoadBootstrap(
      9, true, base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr loaded) {
        bootstrap = std::move(loaded);
        loop.Quit();
      }));
  loop.Run();
  ASSERT_TRUE(bootstrap);
  EXPECT_TRUE(bootstrap->private_profile);
  EXPECT_FALSE(base::PathExists(path));
}

}  // namespace
}  // namespace taffy
