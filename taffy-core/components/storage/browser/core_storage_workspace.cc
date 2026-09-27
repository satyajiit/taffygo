// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_workspace.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"

namespace taffy {

namespace mojom = core_service::mojom;

bool LoadWorkspaceSnapshots(sql::Database* database,
                            mojom::CoreBootstrap* bootstrap) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT workspace_id,revision,snapshot FROM core_workspace "
      "ORDER BY workspace_id"));
  size_t total_bytes = 0u;
  while (query.Step()) {
    if (bootstrap->workspaces.size() >= mojom::kMaxWorkspacesPerProfile) {
      return false;
    }
    const std::string workspace_id = query.ColumnString(0);
    const int64_t revision = query.ColumnInt64(1);
    std::vector<uint8_t> snapshot = query.ColumnBlobAsVector(2);
    if (workspace_id.empty() ||
        workspace_id.size() > mojom::kMaxIdentifierBytes || revision <= 0 ||
        snapshot.empty() ||
        snapshot.size() > mojom::kMaxWorkspaceSnapshotBytes ||
        total_bytes > mojom::kMaxWorkspaceBootstrapBytes - snapshot.size()) {
      return false;
    }
    if (IsWorkspaceDeletionTombstone(snapshot, workspace_id,
                                     static_cast<uint64_t>(revision))) {
      continue;
    }
    WorkspaceSnapshotShape shape;
    if (!DecodeWorkspaceSnapshotShape(
            snapshot, workspace_id, static_cast<uint64_t>(revision), &shape)) {
      return false;
    }
    total_bytes += snapshot.size();
    auto record = mojom::WorkspaceRestoreRecord::New();
    record->workspace_id = workspace_id;
    record->revision = static_cast<uint64_t>(revision);
    record->snapshot = std::move(snapshot);
    bootstrap->workspaces.push_back(std::move(record));
  }
  return query.Succeeded();
}

bool CommitWorkspaceSnapshot(sql::Database* database,
                             const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  if (body.operation_kind != mojom::StorageOperation::kUpsertWorkspace ||
      !body.task_id.empty() || !body.transaction_batch.empty() ||
      !storage_internal::IsNeutralTaskIdSeed(body.task_id_seed) ||
      body.expected_revision != 0u ||
      body.resulting_revision != 0u || body.source_deletion ||
      body.install_skill || body.skill_status || body.skill_run ||
      body.forget_skill || body.assistant_configuration ||
      body.workspace_deletion || body.library_entry || body.library_deletion) {
    return false;
  }
  sql::Transaction transaction(database);
  return transaction.Begin() && WriteWorkspaceSnapshot(database, effect) &&
         transaction.Commit();
}

bool WriteWorkspaceSnapshot(sql::Database* database,
                            const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  WorkspaceSnapshotShape shape;
  if (!body.workspace || body.workspace->snapshot.empty() ||
      body.workspace->snapshot.size() > mojom::kMaxWorkspaceSnapshotBytes ||
      effect.effect_id.empty() ||
      effect.effect_id.size() > mojom::kMaxIdentifierBytes ||
      body.workspace->expected_revision >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      body.workspace->resulting_revision >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      body.workspace->resulting_revision !=
          body.workspace->expected_revision + 1u ||
      !DecodeWorkspaceSnapshotShape(
          body.workspace->snapshot, body.workspace->workspace_id,
          body.workspace->resulting_revision, &shape)) {
    return false;
  }

  {
    sql::Statement duplicate(database->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT workspace_id,revision,snapshot,expected_revision "
        "FROM core_workspace WHERE effect_id=?"));
    duplicate.BindString(0, effect.effect_id);
    if (duplicate.Step()) {
      const bool identical =
          duplicate.ColumnString(0) == body.workspace->workspace_id &&
          duplicate.ColumnInt64(1) ==
              static_cast<int64_t>(body.workspace->resulting_revision) &&
          duplicate.ColumnBlobAsVector(2) == body.workspace->snapshot &&
          duplicate.ColumnInt64(3) ==
              static_cast<int64_t>(body.workspace->expected_revision);
      return identical;
    }
    if (!duplicate.Succeeded()) {
      return false;
    }
  }

  std::optional<uint64_t> durable_revision;
  {
    sql::Statement current(database->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT revision,snapshot FROM core_workspace WHERE workspace_id=?"));
    current.BindString(0, body.workspace->workspace_id);
    if (current.Step()) {
      const int64_t revision = current.ColumnInt64(0);
      if (revision <= 0) {
        return false;
      }
      durable_revision = static_cast<uint64_t>(revision);
      if (IsWorkspaceDeletionTombstone(current.ColumnBlobAsVector(1),
                                       body.workspace->workspace_id,
                                       *durable_revision)) {
        return false;
      }
    } else if (!current.Succeeded()) {
      return false;
    }
  }
  if (durable_revision) {
    if (*durable_revision != body.workspace->expected_revision) {
      return false;
    }
    sql::Statement update(database->GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE core_workspace SET revision=?,snapshot=?,effect_id=?,"
        "expected_revision=? WHERE workspace_id=? AND revision=?"));
    update.BindInt64(0,
                     static_cast<int64_t>(body.workspace->resulting_revision));
    update.BindBlob(1, body.workspace->snapshot);
    update.BindString(2, effect.effect_id);
    update.BindInt64(3,
                     static_cast<int64_t>(body.workspace->expected_revision));
    update.BindString(4, body.workspace->workspace_id);
    update.BindInt64(5,
                     static_cast<int64_t>(body.workspace->expected_revision));
    return update.Run() && database->GetLastChangeCount() == 1;
  }
  if (body.workspace->expected_revision != 0u) {
    return false;
  }
  sql::Statement insert(database->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO core_workspace(workspace_id,revision,snapshot,effect_id,"
      "expected_revision) VALUES(?,?,?,?,?)"));
  insert.BindString(0, body.workspace->workspace_id);
  insert.BindInt64(1, static_cast<int64_t>(body.workspace->resulting_revision));
  insert.BindBlob(2, body.workspace->snapshot);
  insert.BindString(3, effect.effect_id);
  insert.BindInt64(4, static_cast<int64_t>(body.workspace->expected_revision));
  return insert.Run();
}

}  // namespace taffy
