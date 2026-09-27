// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_workspace.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/components/storage/browser/core_storage_schema_test_util.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr char kWorkspaceId[] = "11111111111111111111111111111111";
constexpr char kFirstWorkspaceId[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kSecondWorkspaceId[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr char kPrivateWorkspaceId[] = "cccccccccccccccccccccccccccccccc";

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

std::vector<uint8_t> Snapshot(std::string_view workspace_id,
                              uint64_t revision,
                              std::string_view goal = "test goal",
                              uint32_t schema_version = 5u,
                              uint8_t saved = 1u) {
  std::vector<uint8_t> bytes{'T', 'A', 'F', 'F', 'Y', 'W', 'S', '1'};
  AppendU32(&bytes, schema_version);
  AppendString(&bytes, workspace_id);
  AppendU64(&bytes, revision);
  AppendString(&bytes, goal);
  if (schema_version >= 2u) {
    AppendString(&bytes, "Test workspace");
  }
  bytes.push_back(0u);
  if (schema_version >= 3u) {
    bytes.push_back(saved);
  }
  AppendU64(&bytes, 1u);
  bytes.push_back(0u);
  AppendU32(&bytes, 0u);
  AppendU32(&bytes, 0u);
  return bytes;
}


mojom::CoreBootstrapPtr Load(CoreStorageBroker* broker,
                             bool private_profile = false) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      1, private_profile,
      base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr value) {
        bootstrap = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

mojom::EffectEnvelopePtr WorkspaceEffect(std::string effect_id,
                                         std::string workspace_id,
                                         uint64_t expected,
                                         uint64_t resulting,
                                         std::vector<uint8_t> snapshot) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = effect_id;
  effect->operation->service_generation = 1;
  effect->operation->task_revision = 0;
  effect->operation->deadline_monotonic_ms = 10'000;
  effect->operation->idempotency_key = "workspace-key-" + effect_id;
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  effect->storage_commit->task_id_seed.assign(
      storage_internal::kTaskIdSeedBytes, 0u);
  effect->storage_commit->operation_kind =
      mojom::StorageOperation::kUpsertWorkspace;
  effect->storage_commit->workspace = mojom::WorkspacePersistEffect::New();
  effect->storage_commit->workspace->workspace_id = std::move(workspace_id);
  effect->storage_commit->workspace->snapshot = std::move(snapshot);
  effect->storage_commit->workspace->expected_revision = expected;
  effect->storage_commit->workspace->resulting_revision = resulting;
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

class CoreStorageWorkspaceTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CoreStorageWorkspaceTest, MigratesVersionSixToWorkspaceSchema) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  ASSERT_TRUE(storage_test::CreateHistoricalSchema(&database, 6u));
  database.Close();

  {
    CoreStorageBroker broker(path, false);
    ASSERT_TRUE(Load(&broker));
  }
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(database.Open(path));
  sql::Statement table(database.GetUniqueStatement(
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "name='core_workspace'"));
  ASSERT_TRUE(table.Step());
  EXPECT_EQ("core_workspace", table.ColumnString(0));
}

TEST_F(CoreStorageWorkspaceTest, CommitsReplaysAndRestoresExactSnapshot) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  const std::vector<uint8_t> snapshot = Snapshot(kWorkspaceId, 1u);

  mojom::EffectResultPtr first = Dispatch(
      &broker, WorkspaceEffect("workspace-op-1", kWorkspaceId, 0, 1, snapshot));
  ASSERT_TRUE(first);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, first->status);
  EXPECT_EQ(1u, first->storage->committed_revision);

  mojom::EffectResultPtr replay = Dispatch(
      &broker, WorkspaceEffect("workspace-op-1", kWorkspaceId, 0, 1, snapshot));
  ASSERT_TRUE(replay);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, replay->status);

  mojom::EffectResultPtr stale =
      Dispatch(&broker, WorkspaceEffect("workspace-op-2", kWorkspaceId, 0, 1,
                                        Snapshot(kWorkspaceId, 1u, "stale")));
  ASSERT_TRUE(stale);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, stale->status);

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_EQ(1u, bootstrap->workspaces.size());
  EXPECT_EQ(kWorkspaceId, bootstrap->workspaces.front()->workspace_id);
  EXPECT_EQ(1u, bootstrap->workspaces.front()->revision);
  EXPECT_EQ(snapshot, bootstrap->workspaces.front()->snapshot);
}

TEST_F(CoreStorageWorkspaceTest, TasklessCommitRequiresFixedNeutralTaskSeed) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);

  auto empty_seed = WorkspaceEffect("empty-seed", kFirstWorkspaceId, 0u, 1u,
                                    Snapshot(kFirstWorkspaceId, 1u));
  empty_seed->storage_commit->task_id_seed.clear();
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker, std::move(empty_seed))->status);

  auto nonneutral_seed = WorkspaceEffect(
      "nonneutral-seed", kSecondWorkspaceId, 0u, 1u,
      Snapshot(kSecondWorkspaceId, 1u));
  nonneutral_seed->storage_commit->task_id_seed.front() = 1u;
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker, std::move(nonneutral_seed))->status);

  EXPECT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker,
                     WorkspaceEffect("neutral-seed", kWorkspaceId, 0u, 1u,
                                     Snapshot(kWorkspaceId, 1u)))
                ->status);
}

TEST_F(CoreStorageWorkspaceTest, RefusesMalformedOrMisbindingSnapshots) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  std::vector<uint8_t> malformed = Snapshot(kWorkspaceId, 1u);
  malformed.front() = 0u;
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker, WorkspaceEffect("bad-magic", kWorkspaceId, 0, 1,
                                              std::move(malformed)))
                ->status);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable,
            Dispatch(&broker, WorkspaceEffect("wrong-revision", kWorkspaceId, 0,
                                              1, Snapshot(kWorkspaceId, 2u)))
                ->status);
  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_TRUE(bootstrap->workspaces.empty());
}


TEST_F(CoreStorageWorkspaceTest, RefusesEveryTruncatedSnapshotPrefix) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  const std::vector<uint8_t> complete = Snapshot(kWorkspaceId, 1u);
  for (size_t length = 0u; length < complete.size(); ++length) {
    std::vector<uint8_t> truncated(complete.begin(), complete.begin() + length);
    EXPECT_EQ(mojom::EffectStatus::kUnavailable,
              Dispatch(&broker, WorkspaceEffect(
                                    "truncated-" + std::to_string(length),
                                    kWorkspaceId, 0, 1, std::move(truncated)))
                  ->status)
        << "accepted prefix length " << length;
  }
  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_TRUE(bootstrap->workspaces.empty());
}

TEST_F(CoreStorageWorkspaceTest, LegacySnapshotRemainsRestorable) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker,
                     WorkspaceEffect("legacy-workspace", kWorkspaceId, 0, 1,
                                     Snapshot(kWorkspaceId, 1u, "legacy", 1u)))
                ->status);
  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_EQ(1u, bootstrap->workspaces.size());
  EXPECT_EQ(1u, bootstrap->workspaces.front()->revision);
}

TEST_F(CoreStorageWorkspaceTest, RegularProfilesUseIndependentDatabases) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker first(directory.GetPath().AppendASCII("first.sqlite3"),
                          false);
  CoreStorageBroker second(directory.GetPath().AppendASCII("second.sqlite3"),
                           false);
  ASSERT_EQ(
      mojom::EffectStatus::kCompleted,
      Dispatch(&first, WorkspaceEffect("first-op", kFirstWorkspaceId, 0, 1,
                                       Snapshot(kFirstWorkspaceId, 1u)))
          ->status);
  ASSERT_EQ(
      mojom::EffectStatus::kCompleted,
      Dispatch(&second, WorkspaceEffect("second-op", kSecondWorkspaceId, 0, 1,
                                        Snapshot(kSecondWorkspaceId, 1u)))
          ->status);

  mojom::CoreBootstrapPtr first_bootstrap = Load(&first);
  mojom::CoreBootstrapPtr second_bootstrap = Load(&second);
  ASSERT_TRUE(first_bootstrap);
  ASSERT_TRUE(second_bootstrap);
  ASSERT_EQ(1u, first_bootstrap->workspaces.size());
  ASSERT_EQ(1u, second_bootstrap->workspaces.size());
  EXPECT_EQ(kFirstWorkspaceId,
            first_bootstrap->workspaces.front()->workspace_id);
  EXPECT_EQ(kSecondWorkspaceId,
            second_bootstrap->workspaces.front()->workspace_id);
}

TEST_F(CoreStorageWorkspaceTest, PrivateWorkspaceDisappearsWithProfile) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath unused_path =
      directory.GetPath().AppendASCII("must-not-exist.sqlite3");
  {
    CoreStorageBroker private_broker(base::FilePath(), true);
    ASSERT_EQ(mojom::EffectStatus::kCompleted,
              Dispatch(&private_broker,
                       WorkspaceEffect("private-op", kPrivateWorkspaceId, 0, 1,
                                       Snapshot(kPrivateWorkspaceId, 1u)))
                  ->status);
    mojom::CoreBootstrapPtr bootstrap = Load(&private_broker, true);
    ASSERT_TRUE(bootstrap);
    ASSERT_EQ(1u, bootstrap->workspaces.size());
  }
  task_environment_.RunUntilIdle();

  CoreStorageBroker new_private_broker(base::FilePath(), true);
  mojom::CoreBootstrapPtr empty = Load(&new_private_broker, true);
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->workspaces.empty());
  EXPECT_FALSE(base::PathExists(unused_path));
}

}  // namespace
}  // namespace taffy
