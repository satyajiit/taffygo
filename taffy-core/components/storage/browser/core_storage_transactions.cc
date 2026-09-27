// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include "base/containers/span.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_account_transactions.h"
#include "taffy/components/storage/browser/core_storage_assistant_configuration.h"
#include "taffy/components/storage/browser/core_storage_backend.h"
#include "taffy/components/storage/browser/core_storage_library.h"
#include "taffy/components/storage/browser/core_storage_memory.h"
#include "taffy/components/storage/browser/core_storage_skills.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

bool HasUsableSeed(base::span<const uint8_t> seed) {
  return seed.size() == 32u &&
         std::adjacent_find(seed.begin(), seed.end(), std::not_equal_to<>()) !=
             seed.end();
}

bool IsStorageCommit(const mojom::EffectEnvelope& effect) {
  return effect.kind == mojom::EffectKind::kStorageCommit &&
         effect.storage_commit && !effect.page_observation &&
         !effect.model_request && !effect.network_request &&
         !effect.browser_action && !effect.tool_job && !effect.secure_store &&
         !effect.auth_surface && !effect.permission_request;
}

std::optional<bool> WorkspaceReplayMatches(
    sql::Database* database,
    const mojom::EffectEnvelope& effect) {
  const mojom::WorkspacePersistEffect* workspace =
      effect.storage_commit->workspace.get();
  sql::Statement duplicate(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT workspace_id,revision,snapshot,expected_revision "
      "FROM core_workspace WHERE effect_id=?"));
  duplicate.BindString(0, effect.effect_id);
  if (!duplicate.Step()) {
    return duplicate.Succeeded() ? std::optional<bool>(workspace == nullptr)
                                 : std::nullopt;
  }
  if (!workspace) {
    return false;
  }
  return duplicate.ColumnString(0) == workspace->workspace_id &&
         duplicate.ColumnInt64(1) ==
             static_cast<int64_t>(workspace->resulting_revision) &&
         duplicate.ColumnBlobAsVector(2) == workspace->snapshot &&
         duplicate.ColumnInt64(3) ==
             static_cast<int64_t>(workspace->expected_revision);
}

}  // namespace

bool CoreStorageBroker::Backend::CommitIntent(mojom::EffectEnvelopePtr effect) {
  // STORAGE_COMMIT is the canonical transaction batch itself. It bypasses the
  // outer effect journal so command/events/audit/effect intents are written
  // exactly once by CommitStorage rather than double-journalled.
  if (!effect || !effect->operation || IsStorageCommit(*effect) ||
      !EnsureOpen()) {
    return false;
  }
  sql::Transaction transaction(&database_);
  if (!transaction.Begin()) {
    return false;
  }
  const bool account_session_mutation =
      storage_internal::IsAccountSessionMutation(*effect);
  if (account_session_mutation) {
    const std::optional<bool> pending =
        storage_internal::HasAnyAccountSessionPendingMarker(&database_);
    if (!pending || *pending) {
      return false;
    }
  }
  sql::Statement insert(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR IGNORE INTO core_effect_journal("
      "effect_id,generation,operation_id,task_revision,idempotency_key,"
      "effect_kind,retry_class) VALUES(?,?,?,?,?,?,?)"));
  insert.BindString(0, effect->effect_id);
  insert.BindInt64(1,
                   static_cast<int64_t>(effect->operation->service_generation));
  insert.BindString(2, effect->operation->operation_id);
  insert.BindInt64(3, static_cast<int64_t>(effect->operation->task_revision));
  insert.BindString(4, effect->operation->idempotency_key);
  insert.BindInt(5, static_cast<int>(effect->kind));
  insert.BindInt(6, static_cast<int>(effect->retry_class));
  // An existing ID is a durable execution claim, whether its row is still
  // pending or already terminal. Reattaching would permit a browser restart
  // to dispatch the same consequential work twice.
  if (!insert.Run() || database_.GetLastChangeCount() != 1) {
    return false;
  }
  if (account_session_mutation) {
    sql::Statement mark(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO core_account_session_pending(effect_id) VALUES(?)"));
    mark.BindString(0, effect->effect_id);
    if (!mark.Run() || database_.GetLastChangeCount() != 1) {
      return false;
    }
  }
  return transaction.Commit();
}

bool CoreStorageBroker::Backend::CommitResult(mojom::EffectResultPtr result) {
  if (!result || !result->operation ||
      result->kind == mojom::EffectKind::kStorageCommit || !EnsureOpen()) {
    return false;
  }
  sql::Transaction transaction(&database_);
  if (!transaction.Begin()) {
    return false;
  }
  const std::optional<bool> has_pending_session_mutation =
      storage_internal::HasAccountSessionPendingMarker(&database_,
                                                       result->effect_id);
  if (!has_pending_session_mutation ||
      (storage_internal::IsAccountSessionMutation(*result) &&
       !*has_pending_session_mutation)) {
    return false;
  }
  sql::Statement update(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_effect_journal SET terminal_status=? WHERE effect_id=? "
      "AND generation=? AND effect_kind=? AND terminal_status IS NULL"));
  update.BindInt(0, static_cast<int>(result->status));
  update.BindString(1, result->effect_id);
  update.BindInt64(2,
                   static_cast<int64_t>(result->operation->service_generation));
  update.BindInt(3, static_cast<int>(result->kind));
  if (!update.Run() || database_.GetLastChangeCount() != 1 ||
      !storage_internal::PersistAccountResult(&database_, *result,
                                              *has_pending_session_mutation)) {
    return false;
  }
  // OUTCOME_UNKNOWN is the only terminal state that deliberately preserves
  // the barrier. The account adapter must reconcile its vault first, then use
  // FinishAccountReconciliation() to clear both physical halves.
  if (*has_pending_session_mutation &&
      result->status != mojom::EffectStatus::kOutcomeUnknown) {
    sql::Statement clear(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "DELETE FROM core_account_session_pending WHERE effect_id=?"));
    clear.BindString(0, result->effect_id);
    if (!clear.Run() || database_.GetLastChangeCount() != 1) {
      return false;
    }
  }
  return transaction.Commit();
}

bool CoreStorageBroker::Backend::CommitStorage(
    mojom::EffectEnvelopePtr effect) {
  if (!effect || !effect->operation || !IsStorageCommit(*effect) ||
      !EnsureOpen()) {
    return false;
  }
  switch (effect->storage_commit->operation_kind) {
    case mojom::StorageOperation::kUpsertWorkspace:
      return CommitWorkspaceSnapshot(&database_, *effect);
    case mojom::StorageOperation::kDeleteWorkspace: {
      if (!effect->storage_commit->workspace_deletion) {
        return false;
      }
      const mojom::WorkspaceDeletionEffect& deletion =
          *effect->storage_commit->workspace_deletion;
      WorkspaceDeletionRequest request;
      request.effect_id = effect->effect_id;
      request.workspace_id = deletion.workspace_id;
      request.expected_revision = deletion.expected_revision;
      request.resulting_revision = deletion.resulting_revision;
      request.counts.sources = deletion.sources;
      request.counts.facts = deletion.facts;
      request.counts.artifact_metadata = deletion.artifact_metadata;
      request.counts.derived_indexes = deletion.derived_indexes;
      request.confirmation_token = deletion.confirmation_token;
      return CommitWorkspaceDeletion(&database_, request);
    }
    case mojom::StorageOperation::kUpsertLibraryEntry:
      return CommitLibraryEntry(&database_, *effect);
    case mojom::StorageOperation::kRemoveLibraryEntry:
      return CommitLibraryDeletion(&database_, *effect);
    case mojom::StorageOperation::kUpsertMemory:
      return !ephemeral_ && CommitMemoryRecord(&database_, *effect);
    case mojom::StorageOperation::kDeleteMemory:
      return !ephemeral_ && CommitMemoryDeletion(&database_, *effect);
    case mojom::StorageOperation::kDeleteSource:
      return CommitSourceDeletion(&database_, *effect);
    case mojom::StorageOperation::kInstallSkill:
    case mojom::StorageOperation::kSetSkillStatus:
    case mojom::StorageOperation::kRecordSkillRun:
    case mojom::StorageOperation::kForgetSkill:
      return CommitSkillOperation(&database_, *effect);
    case mojom::StorageOperation::kSetAssistantConfiguration:
      return CommitAssistantConfiguration(&database_, *effect);
    case mojom::StorageOperation::kQueryWorkspace:
      // There is no query surface, and its absence is the decision rather than
      // an omission. What a shipping durable store may be asked is OD-104.
      return false;
    case mojom::StorageOperation::kAppendTaskCommit:
      break;
  }
  if (effect->storage_commit->source_deletion ||
      effect->storage_commit->install_skill ||
      effect->storage_commit->skill_status ||
      effect->storage_commit->skill_run ||
      effect->storage_commit->forget_skill ||
      effect->storage_commit->assistant_configuration ||
      effect->storage_commit->workspace_deletion ||
      effect->storage_commit->library_entry ||
      effect->storage_commit->library_deletion ||
      effect->storage_commit->memory_record ||
      effect->storage_commit->memory_deletion ||
      effect->storage_commit->task_id.empty() ||
      effect->storage_commit->transaction_batch.empty() ||
      !HasUsableSeed(effect->storage_commit->task_id_seed)) {
    return false;
  }
  const mojom::StorageCommitEffect& body = *effect->storage_commit;
  if (body.resulting_revision <= body.expected_revision ||
      body.transaction_batch.size() > mojom::kMaxQueuedBytesPerProfile) {
    return false;
  }

  sql::Transaction transaction(&database_);
  if (!transaction.Begin()) {
    return false;
  }
  std::optional<bool> duplicate_is_identical;
  {
    sql::Statement duplicate(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT c.task_id,c.expected_revision,c.resulting_revision,"
        "c.transaction_batch,a.task_id_seed FROM core_task_commit c "
        "JOIN core_task_aggregate a ON a.task_id=c.task_id "
        "WHERE c.effect_id=?"));
    duplicate.BindString(0, effect->effect_id);
    if (duplicate.Step()) {
      const std::vector<uint8_t> stored_batch = duplicate.ColumnBlobAsVector(3);
      const std::vector<uint8_t> stored_seed = duplicate.ColumnBlobAsVector(4);
      duplicate_is_identical =
          duplicate.ColumnString(0) == body.task_id &&
          duplicate.ColumnInt64(1) ==
              static_cast<int64_t>(body.expected_revision) &&
          duplicate.ColumnInt64(2) ==
              static_cast<int64_t>(body.resulting_revision) &&
          stored_batch == body.transaction_batch &&
          stored_seed.size() == body.task_id_seed.size() &&
          std::equal(stored_seed.begin(), stored_seed.end(),
                     body.task_id_seed.begin());
    } else if (!duplicate.Succeeded()) {
      return false;
    }
  }
  if (duplicate_is_identical) {
    if (!*duplicate_is_identical) {
      return false;
    }
    const std::optional<bool> workspace_is_identical =
        WorkspaceReplayMatches(&database_, *effect);
    return workspace_is_identical && *workspace_is_identical &&
           transaction.Commit();
  }

  bool aggregate_exists = false;
  {
    sql::Statement aggregate(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT task_id_seed FROM core_task_aggregate WHERE task_id=?"));
    aggregate.BindString(0, body.task_id);
    if (aggregate.Step()) {
      aggregate_exists = true;
      const std::vector<uint8_t> stored_seed = aggregate.ColumnBlobAsVector(0);
      if (stored_seed.size() != body.task_id_seed.size() ||
          !std::equal(stored_seed.begin(), stored_seed.end(),
                      body.task_id_seed.begin())) {
        return false;
      }
    } else if (!aggregate.Succeeded()) {
      return false;
    }
  }
  if (!aggregate_exists) {
    if (body.expected_revision != storage_schema::kInitialTaskRevision) {
      return false;
    }
    sql::Statement create_aggregate(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO core_task_aggregate(task_id,task_id_seed) VALUES(?,?)"));
    create_aggregate.BindString(0, body.task_id);
    create_aggregate.BindBlob(1, base::span(body.task_id_seed));
    if (!create_aggregate.Run()) {
      return false;
    }
  }

  uint64_t durable_revision = storage_schema::kInitialTaskRevision;
  {
    sql::Statement current(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT resulting_revision FROM core_task_commit WHERE task_id=? "
        "ORDER BY sequence DESC LIMIT 1"));
    current.BindString(0, body.task_id);
    if (current.Step()) {
      const int64_t value = current.ColumnInt64(0);
      if (value < 0) {
        return false;
      }
      durable_revision = static_cast<uint64_t>(value);
    } else if (!current.Succeeded()) {
      return false;
    }
  }
  if (durable_revision != body.expected_revision) {
    return false;
  }

  {
    sql::Statement insert(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO core_task_commit(effect_id,task_id,expected_revision,"
        "resulting_revision,transaction_batch) VALUES(?,?,?,?,?)"));
    insert.BindString(0, effect->effect_id);
    insert.BindString(1, body.task_id);
    insert.BindInt64(2, static_cast<int64_t>(body.expected_revision));
    insert.BindInt64(3, static_cast<int64_t>(body.resulting_revision));
    insert.BindBlob(4, body.transaction_batch);
    if (!insert.Run()) {
      return false;
    }
  }
  if (body.workspace && !WriteWorkspaceSnapshot(&database_, *effect)) {
    return false;
  }
  return transaction.Commit();
}

void CoreStorageBroker::Backend::MarkEffectsLost(
    uint64_t generation,
    std::vector<std::string> effect_ids) {
  if (effect_ids.empty() || !EnsureOpen()) {
    return;
  }
  sql::Transaction transaction(&database_);
  if (!transaction.Begin()) {
    return;
  }
  for (const std::string& effect_id : effect_ids) {
    sql::Statement update(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE core_effect_journal SET terminal_status="
        "CASE WHEN retry_class=? THEN ? ELSE ? END "
        "WHERE effect_id=? AND generation=? AND terminal_status IS NULL"));
    update.BindInt(0, static_cast<int>(mojom::RetryClass::kConsequential));
    update.BindInt(1, static_cast<int>(mojom::EffectStatus::kOutcomeUnknown));
    update.BindInt(2, static_cast<int>(mojom::EffectStatus::kUnavailable));
    update.BindString(3, effect_id);
    update.BindInt64(4, static_cast<int64_t>(generation));
    if (!update.Run()) {
      return;
    }
  }
  transaction.Commit();
}

}  // namespace taffy
