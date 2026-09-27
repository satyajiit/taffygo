// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Deleting one workspace: the confirmed transaction, its rollback under a
// disk fault, and the revision the browser reports back for it.

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
#include "sql/test/drive_error_test_vfs.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
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

mojom::EffectEnvelopePtr WorkspaceDeletionEffect(std::string effect_id,
                                                 std::string workspace_id,
                                                 uint64_t expected,
                                                 uint64_t resulting) {
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
      mojom::StorageOperation::kDeleteWorkspace;
  effect->storage_commit->workspace_deletion =
      mojom::WorkspaceDeletionEffect::New();
  auto& deletion = effect->storage_commit->workspace_deletion;
  deletion->workspace_id = std::move(workspace_id);
  deletion->expected_revision = expected;
  deletion->resulting_revision = resulting;
  deletion->confirmation_token = std::string(64u, 'a');
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

class CoreStorageWorkspaceDeleteTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CoreStorageWorkspaceDeleteTest,
       ConfirmedDeletionReplaysExactlyAndStaysDeletedAfterRestart) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_EQ(
        mojom::EffectStatus::kCompleted,
        Dispatch(&broker, WorkspaceEffect("create-before-delete", kWorkspaceId,
                                          0, 1, Snapshot(kWorkspaceId, 1u)))
            ->status);
  }
  task_environment_.RunUntilIdle();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  WorkspaceDeletionRequest request;
  request.effect_id = "delete-workspace";
  request.workspace_id = kWorkspaceId;
  request.expected_revision = 1u;
  request.resulting_revision = 2u;
  request.confirmation_token = std::string(64u, 'a');
  EXPECT_TRUE(CommitWorkspaceDeletion(&database, request));
  EXPECT_TRUE(CommitWorkspaceDeletion(&database, request));
  WorkspaceDeletionRequest changed = request;
  changed.confirmation_token = std::string(64u, 'b');
  EXPECT_FALSE(CommitWorkspaceDeletion(&database, changed));

  {
    sql::Statement row(database.GetUniqueStatement(
        "SELECT revision,snapshot FROM core_workspace WHERE workspace_id=?"));
    row.BindString(0, kWorkspaceId);
    ASSERT_TRUE(row.Step());
    EXPECT_EQ(2, row.ColumnInt64(0));
    const std::vector<uint8_t> tombstone = row.ColumnBlobAsVector(1);
    EXPECT_TRUE(IsWorkspaceDeletionTombstone(tombstone, kWorkspaceId, 2u));
    for (size_t length = 0u; length < tombstone.size(); ++length) {
      EXPECT_FALSE(IsWorkspaceDeletionTombstone(
          base::span(tombstone).first(length), kWorkspaceId, 2u));
    }
  }
  database.Close();

  CoreStorageBroker restarted(path, false);
  mojom::CoreBootstrapPtr bootstrap = Load(&restarted);
  ASSERT_TRUE(bootstrap);
  EXPECT_TRUE(bootstrap->workspaces.empty());
  EXPECT_EQ(
      mojom::EffectStatus::kUnavailable,
      Dispatch(&restarted, WorkspaceEffect("resurrection", kWorkspaceId, 2, 3,
                                           Snapshot(kWorkspaceId, 3u)))
          ->status);
}

TEST_F(CoreStorageWorkspaceDeleteTest, DeletionFaultRollsBackTheExactSnapshot) {
  sql::test::DriveErrorTestVfs vfs;
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  const std::vector<uint8_t> snapshot = Snapshot(kWorkspaceId, 1u);
  {
    CoreStorageBroker broker(path, false);
    ASSERT_EQ(mojom::EffectStatus::kCompleted,
              Dispatch(&broker, WorkspaceEffect("create-for-rollback",
                                                kWorkspaceId, 0, 1, snapshot))
                  ->status);
  }
  task_environment_.RunUntilIdle();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  WorkspaceDeletionRequest request;
  request.effect_id = "delete-fault";
  request.workspace_id = kWorkspaceId;
  request.expected_revision = 1u;
  request.resulting_revision = 2u;
  request.confirmation_token = std::string(64u, 'c');
  vfs.set_drive_full(true);
  EXPECT_FALSE(CommitWorkspaceDeletion(&database, request));
  vfs.set_drive_full(false);
  ASSERT_FALSE(vfs.errors_produced().empty());
  EXPECT_EQ(sql::SqliteErrorCode::kFullDisk, vfs.errors_produced().back());
  sql::Statement row(
      database.GetUniqueStatement("SELECT revision,snapshot,effect_id FROM "
                                  "core_workspace WHERE workspace_id=?"));
  row.BindString(0, kWorkspaceId);
  ASSERT_TRUE(row.Step());
  EXPECT_EQ(1, row.ColumnInt64(0));
  EXPECT_EQ(snapshot, row.ColumnBlobAsVector(1));
  EXPECT_EQ("create-for-rollback", row.ColumnString(2));
}

TEST_F(CoreStorageWorkspaceDeleteTest,
       AConfirmedDeletionReportsTheRevisionItLandedAt) {
  // The core stages a deletion at one revision past the workspace it is
  // removing and waits for the browser to say it landed there. The broker
  // reported the envelope's top-level revision, which a deletion never sets,
  // so every discard came home at revision 0: `complete_deletion` answered
  // WrongCompletion, no plane claimed the completion, and the deletion stayed
  // pending for the life of the process. On a phone that was a confirmed
  // "Discard temporary workspace" whose button never left, a row reading
  // "Working on it…" that never resolved, and nothing deleted.
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  ASSERT_EQ(mojom::EffectStatus::kCompleted,
            Dispatch(&broker, WorkspaceEffect("create-then-delete",
                                              kWorkspaceId, 0u, 1u,
                                              Snapshot(kWorkspaceId, 1u)))
                ->status);

  const mojom::EffectResultPtr deleted = Dispatch(
      &broker, WorkspaceDeletionEffect("delete-it", kWorkspaceId, 1u, 2u));
  ASSERT_TRUE(deleted);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, deleted->status);
  ASSERT_TRUE(deleted->storage);
  EXPECT_EQ(2u, deleted->storage->committed_revision);
}

}  // namespace
}  // namespace taffy
