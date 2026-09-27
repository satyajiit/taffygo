// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_memory.h"

#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/components/storage/browser/core_storage_memory_validation.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr char kMemoryId[] = "11111111111111111111111111111111";
constexpr char kWorkspaceId[] = "22222222222222222222222222222222";
constexpr char kDeletedStatementCanary[] =
    "TAFFY_MEMORY_DELETION_CANARY_6b9f57259aed";

mojom::CoreBootstrapPtr Load(CoreStorageBroker* broker, bool private_profile) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      1u, private_profile,
      base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr value) {
        bootstrap = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

mojom::EffectResultPtr Dispatch(CoreStorageBroker* broker,
                                mojom::EffectEnvelopePtr effect) {
  base::RunLoop loop;
  mojom::EffectResultPtr result;
  broker->DispatchStorage(
      std::move(effect),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr value) {
        result = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return result;
}

mojom::EffectEnvelopePtr BaseEffect(std::string effect_id,
                                    uint64_t expected,
                                    uint64_t resulting) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = effect_id;
  effect->operation->service_generation = 1u;
  effect->operation->deadline_monotonic_ms = 10'000u;
  effect->operation->idempotency_key = "memory-key-" + effect_id;
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->task_id_seed.assign(
      storage_internal::kTaskIdSeedBytes, 0u);
  effect->storage_commit->expected_revision = expected;
  effect->storage_commit->resulting_revision = resulting;
  return effect;
}

mojom::EffectEnvelopePtr SaveEffect(
    std::string effect_id,
    uint64_t expected,
    uint64_t resulting,
    uint64_t expected_record_revision = 0u,
    std::string statement = "Prefer concise answers") {
  auto effect = BaseEffect(std::move(effect_id), expected, resulting);
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kUpsertMemory;
  auto record = mojom::MemoryRecord::New();
  record->memory_id = kMemoryId;
  record->revision = expected_record_revision + 1u;
  record->statement = std::move(statement);
  record->source_kind = mojom::MemorySourceKind::kAcceptedTaskSuggestion;
  record->source_task_id = "task-1";
  record->source_workspace =
      mojom::MemoryWorkspaceRecord::New(kWorkspaceId, "TV research");
  record->scope_kind = mojom::MemoryScopeKind::kWorkspace;
  record->scope_workspace =
      mojom::MemoryWorkspaceRecord::New(kWorkspaceId, "TV research");
  record->sensitivity = mojom::MemorySensitivity::kSensitive;
  record->created_at_epoch_ms = 1'000u;
  record->updated_at_epoch_ms = 2'000u + expected_record_revision;
  record->reviewed_at_epoch_ms = 2'000u;
  record->expires_at_epoch_ms = 10'000u;
  effect->storage_commit->memory_record = mojom::MemoryPersistEffect::New(
      std::move(record), expected_record_revision);
  return effect;
}

mojom::EffectEnvelopePtr DeleteEffect(std::string effect_id,
                                      uint64_t expected,
                                      uint64_t resulting,
                                      uint64_t expected_record_revision) {
  auto effect = BaseEffect(std::move(effect_id), expected, resulting);
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kDeleteMemory;
  effect->storage_commit->memory_deletion =
      mojom::MemoryDeletionEffect::New(kMemoryId, expected_record_revision,
                                       expected_record_revision + 1u, 3'000u);
  return effect;
}

class CoreStorageMemoryTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CoreStorageMemoryTest, TypedBodiesAreClosedAndRevisionExact) {
  auto save = SaveEffect("memory-save-1", 0u, 1u);
  ASSERT_TRUE(save->storage_commit);
  EXPECT_TRUE(IsValidMemoryStorageCommitBody(*save->storage_commit));

  save->storage_commit->memory_record->record->revision = 2u;
  EXPECT_FALSE(IsValidMemoryStorageCommitBody(*save->storage_commit));
  save->storage_commit->memory_record->record->revision = 1u;
  save->storage_commit->memory_deletion =
      mojom::MemoryDeletionEffect::New(kMemoryId, 1u, 2u, 3'000u);
  EXPECT_FALSE(IsValidMemoryStorageCommitBody(*save->storage_commit));

  auto removal = DeleteEffect("memory-delete-1", 1u, 2u, 1u);
  ASSERT_TRUE(removal->storage_commit);
  EXPECT_TRUE(IsValidMemoryStorageCommitBody(*removal->storage_commit));
  removal->storage_commit->memory_deletion->resulting_record_revision = 3u;
  EXPECT_FALSE(IsValidMemoryStorageCommitBody(*removal->storage_commit));
}

TEST_F(CoreStorageMemoryTest, CommitsReplaysUpdatesAndRestoresExactRecord) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);

  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker, SaveEffect("memory-save-1", 0u, 1u))->status);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker, SaveEffect("memory-save-1", 0u, 1u))->status);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker,
                     SaveEffect("memory-save-1", 0u, 1u, 0u, "changed replay"))
                ->status);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker, SaveEffect("memory-stale", 0u, 1u))->status);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker, SaveEffect("memory-update-1", 1u, 2u, 1u,
                                         "Prefer short checked answers"))
                ->status);

  mojom::CoreBootstrapPtr bootstrap = Load(&broker, false);
  ASSERT_TRUE(bootstrap);
  EXPECT_EQ(2u, bootstrap->memory_revision);
  ASSERT_EQ(1u, bootstrap->memory_records.size());
  const mojom::MemoryRecord& record = *bootstrap->memory_records.front();
  EXPECT_EQ(kMemoryId, record.memory_id);
  EXPECT_EQ(2u, record.revision);
  EXPECT_EQ("Prefer short checked answers", record.statement);
  EXPECT_EQ("task-1", record.source_task_id);
  ASSERT_TRUE(record.scope_workspace);
  EXPECT_EQ(kWorkspaceId, record.scope_workspace->workspace_id);
  EXPECT_EQ(mojom::MemorySensitivity::kSensitive, record.sensitivity);
}

TEST_F(CoreStorageMemoryTest,
       DeletionRemovesContentAndLeavesContentFreeReplay) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_EQ(mojom::EffectStatus::kCompleted,
              Dispatch(&broker, SaveEffect("memory-save-1", 0u, 1u, 0u,
                                           kDeletedStatementCanary))
                  ->status);
  }
  task_environment_.RunUntilIdle();

  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(path, &bytes));
  ASSERT_NE(std::string::npos, bytes.find(kDeletedStatementCanary));

  {
    CoreStorageBroker broker(path, false);
    ASSERT_EQ(
        mojom::EffectStatus::kCompleted,
        Dispatch(&broker, DeleteEffect("memory-delete-1", 1u, 2u, 1u))->status);
    EXPECT_EQ(
        mojom::EffectStatus::kCompleted,
        Dispatch(&broker, DeleteEffect("memory-delete-1", 1u, 2u, 1u))->status);

    mojom::CoreBootstrapPtr bootstrap = Load(&broker, false);
    ASSERT_TRUE(bootstrap);
    EXPECT_EQ(2u, bootstrap->memory_revision);
    EXPECT_TRUE(bootstrap->memory_records.empty());
  }
  task_environment_.RunUntilIdle();

  bytes.clear();
  ASSERT_TRUE(base::ReadFileToString(path, &bytes));
  EXPECT_EQ(std::string::npos, bytes.find(kDeletedStatementCanary));

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  sql::Statement content(database.GetUniqueStatement(
      "SELECT COUNT(*) FROM core_memory_record WHERE memory_id=?"));
  content.BindString(0, kMemoryId);
  ASSERT_TRUE(content.Step());
  EXPECT_EQ(0, content.ColumnInt(0));

  sql::Statement tombstone(database.GetUniqueStatement(
      "SELECT revision,deleted_at_epoch_ms,expected_record_revision,effect_id "
      "FROM core_memory_tombstone WHERE memory_id=?"));
  tombstone.BindString(0, kMemoryId);
  ASSERT_TRUE(tombstone.Step());
  EXPECT_EQ(2, tombstone.ColumnInt(0));
  EXPECT_EQ(3'000, tombstone.ColumnInt64(1));
  EXPECT_EQ(1, tombstone.ColumnInt(2));
  EXPECT_EQ("memory-delete-1", tombstone.ColumnString(3));
}

TEST_F(CoreStorageMemoryTest, PrivateProfilePublishesNoMemoryAndRejectsWrites) {
  CoreStorageBroker broker(base::FilePath(), true);
  mojom::CoreBootstrapPtr bootstrap = Load(&broker, true);
  ASSERT_TRUE(bootstrap);
  EXPECT_EQ(0u, bootstrap->memory_revision);
  EXPECT_TRUE(bootstrap->memory_records.empty());

  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker, SaveEffect("memory-save-1", 0u, 1u))->status);
  bootstrap = Load(&broker, true);
  ASSERT_TRUE(bootstrap);
  EXPECT_EQ(0u, bootstrap->memory_revision);
  EXPECT_TRUE(bootstrap->memory_records.empty());
}

}  // namespace
}  // namespace taffy
