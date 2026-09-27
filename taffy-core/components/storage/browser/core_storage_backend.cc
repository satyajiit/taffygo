// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_backend.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/account_session_expiry.h"
#include "taffy/components/storage/browser/core_storage_assistant_configuration.h"
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

std::optional<mojom::AccountAuthMethod> AccountAuthMethodFromStorage(
    int value) {
  switch (value) {
    case static_cast<int>(mojom::AccountAuthMethod::kGoogle):
      return mojom::AccountAuthMethod::kGoogle;
    case static_cast<int>(mojom::AccountAuthMethod::kEmailLink):
      return mojom::AccountAuthMethod::kEmailLink;
    case static_cast<int>(mojom::AccountAuthMethod::kGithub):
      return mojom::AccountAuthMethod::kGithub;
    case static_cast<int>(mojom::AccountAuthMethod::kFacebook):
      return mojom::AccountAuthMethod::kFacebook;
    default:
      return std::nullopt;
  }
}

bool ReadCommittedAccountSession(
    sql::Database* database,
    mojom::AccountSessionHandlePtr* committed_session,
    bool* invalid_session) {
  *committed_session = nullptr;
  *invalid_session = false;
  sql::Statement account(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT session_handle,account_subject,expires_at_utc_ms,rotation,"
      "auth_method,email,display_name FROM core_account_session "
      "WHERE singleton=1"));
  if (!account.Step()) {
    return account.Succeeded();
  }

  const std::string session_handle = account.ColumnString(0);
  const std::string account_subject = account.ColumnString(1);
  const int64_t expires_at_utc_ms = account.ColumnInt64(2);
  const int64_t rotation = account.ColumnInt64(3);
  const std::optional<mojom::AccountAuthMethod> auth_method =
      AccountAuthMethodFromStorage(account.ColumnInt(4));
  // NULL is the ordinary answer and means the provider named nothing. A row
  // that holds an empty string or an oversized one is a row nothing here wrote,
  // so the session is treated as unreadable rather than partially believed.
  std::optional<std::string> email;
  std::optional<std::string> display_name;
  bool identity_readable = true;
  if (account.GetColumnType(5) != sql::ColumnType::kNull) {
    email = account.ColumnString(5);
    identity_readable =
        !email->empty() && email->size() <= mojom::kMaxAccountEmailBytes;
  }
  if (identity_readable && account.GetColumnType(6) != sql::ColumnType::kNull) {
    display_name = account.ColumnString(6);
    identity_readable =
        !display_name->empty() &&
        display_name->size() <= mojom::kMaxAccountDisplayNameBytes;
  }
  const std::optional<uint64_t> monotonic_expiry =
      expires_at_utc_ms > 0
          ? RestampAccountSessionExpiry(
                base::Time::FromMillisecondsSinceUnixEpoch(expires_at_utc_ms),
                base::Time::Now(), base::TimeTicks::Now())
          : std::nullopt;
  if (session_handle.empty() ||
      session_handle.size() > mojom::kMaxIdentifierBytes ||
      account_subject.empty() ||
      account_subject.size() > mojom::kMaxIdentifierBytes || rotation < 0 ||
      !auth_method || !monotonic_expiry || !identity_readable) {
    *invalid_session = true;
    return true;
  }

  *committed_session = mojom::AccountSessionHandle::New(
      session_handle, account_subject, *monotonic_expiry,
      static_cast<uint64_t>(rotation), *auth_method, std::move(email),
      std::move(display_name));
  return true;
}

std::optional<bool> HasPendingAccountSessionMutation(sql::Database* database) {
  sql::Statement pending(database->GetCachedStatement(
      SQL_FROM_HERE, "SELECT 1 FROM core_account_session_pending LIMIT 1"));
  if (pending.Step()) {
    return true;
  }
  return pending.Succeeded() ? std::optional<bool>(false) : std::nullopt;
}

bool DeleteCommittedAccountSession(sql::Database* database) {
  sql::Statement clear(database->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM core_account_session WHERE singleton=1"));
  return clear.Run();
}

bool ApplyKnownMigration(sql::Database* database,
                         int recorded_version,
                         const std::string& recorded_checksum) {
  for (const storage_schema::Migration& migration :
       storage_schema::kMigrations) {
    if (recorded_version != static_cast<int>(migration.from_version) ||
        recorded_checksum != migration.from_checksum ||
        migration.statement_offset >
            storage_schema::kMigrationStatements.size() ||
        migration.statement_count >
            storage_schema::kMigrationStatements.size() -
                migration.statement_offset) {
      continue;
    }
    for (size_t index = 0; index < migration.statement_count; ++index) {
      if (!database->Execute(
              storage_schema::kMigrationStatements[migration.statement_offset +
                                                   index])) {
        return false;
      }
    }
    sql::Statement update(database->GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE taffy_storage_schema SET version=?,checksum=? "
        "WHERE component=? AND version=? AND checksum=?"));
    update.BindInt(0, static_cast<int>(storage_schema::kVersion));
    update.BindString(1, storage_schema::kChecksum);
    update.BindString(2, storage_schema::kComponent);
    update.BindInt(3, recorded_version);
    update.BindString(4, recorded_checksum);
    return update.Run() && database->GetLastChangeCount() == 1;
  }
  return false;
}

}  // namespace

CoreStorageBroker::Backend::Backend(base::FilePath database_path,
                                    bool ephemeral)
    : database_path_(std::move(database_path)),
      ephemeral_(ephemeral),
      database_(sql::DatabaseOptions().set_exclusive_locking(true),
                sql::Database::Tag("TaffyCore")) {}

CoreStorageBroker::Backend::~Backend() = default;

mojom::CoreBootstrapPtr CoreStorageBroker::Backend::LoadBootstrap(
    uint64_t generation,
    bool private_profile) {
  if (private_profile != ephemeral_ || !EnsureOpen()) {
    return nullptr;
  }

  ObserveBackupRestoreGeneration(generation);

  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = generation;
  bootstrap->private_profile = private_profile;
  bootstrap->core_journal_schema_version = storage_schema::kVersion;
  bootstrap->core_journal_schema_checksum =
      std::string(storage_schema::kChecksum);

  sql::Statement profile_identity(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT browser_profile_id FROM core_profile_identity "
      "WHERE singleton=1"));
  if (profile_identity.Step()) {
    bootstrap->browser_profile_id = profile_identity.ColumnString(0);
    if (bootstrap->browser_profile_id.empty()) {
      return nullptr;
    }
  } else if (!profile_identity.Succeeded()) {
    return nullptr;
  } else {
    bootstrap->browser_profile_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    if (bootstrap->browser_profile_id.empty()) {
      return nullptr;
    }
    sql::Statement insert_identity(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO core_profile_identity(singleton,browser_profile_id) "
        "VALUES(1,?)"));
    insert_identity.BindString(0, bootstrap->browser_profile_id);
    if (!insert_identity.Run()) {
      return nullptr;
    }
  }

  mojom::AccountSessionHandlePtr committed_session;
  bool invalid_session = false;
  const std::optional<bool> pending_account_mutation =
      HasPendingAccountSessionMutation(&database_);
  if (!pending_account_mutation ||
      !ReadCommittedAccountSession(&database_, &committed_session,
                                   &invalid_session)) {
    return nullptr;
  }
  if (invalid_session && !DeleteCommittedAccountSession(&database_)) {
    return nullptr;
  }
  // A dispatched session mutation without an exact terminal result is an
  // account reconciliation barrier. Never let a later utility generation
  // restore the old SQL checkpoint while that barrier remains durable.
  if (!invalid_session && !*pending_account_mutation) {
    bootstrap->account_session = std::move(committed_session);
  }

  sql::Statement statement(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT c.task_id,a.task_id_seed,c.effect_id,c.expected_revision,"
      "c.resulting_revision,c.transaction_batch FROM core_task_commit c "
      "JOIN core_task_aggregate a ON a.task_id=c.task_id "
      "ORDER BY c.task_id,c.sequence"));
  std::string current_task_id;
  mojom::TaskRestoreRecord* current_task = nullptr;
  size_t total_bytes = 0;
  while (statement.Step()) {
    const std::string task_id = statement.ColumnString(0);
    if (!current_task || task_id != current_task_id) {
      const std::vector<uint8_t> seed = statement.ColumnBlobAsVector(1);
      if (!HasUsableSeed(seed)) {
        return nullptr;
      }
      auto task = mojom::TaskRestoreRecord::New();
      task->task_id = task_id;
      task->task_id_seed.resize(seed.size());
      std::copy(seed.begin(), seed.end(), task->task_id_seed.begin());
      bootstrap->tasks.push_back(std::move(task));
      current_task = bootstrap->tasks.back().get();
      current_task_id = task_id;
    }

    auto batch = mojom::CommittedTaskBatch::New();
    batch->effect_id = statement.ColumnString(2);
    const int64_t expected = statement.ColumnInt64(3);
    const int64_t resulting = statement.ColumnInt64(4);
    if (expected < 0 || resulting < 0) {
      return nullptr;
    }
    batch->expected_revision = static_cast<uint64_t>(expected);
    batch->resulting_revision = static_cast<uint64_t>(resulting);
    batch->transaction_batch = statement.ColumnBlobAsVector(5);
    if (batch->transaction_batch.size() > mojom::kMaxQueuedBytesPerProfile ||
        total_bytes > mojom::kMaxQueuedBytesPerProfile -
                          batch->transaction_batch.size()) {
      return nullptr;
    }
    total_bytes += batch->transaction_batch.size();
    current_task->batches.push_back(std::move(batch));
  }
  return statement.Succeeded() &&
                 LoadWorkspaceSnapshots(&database_, bootstrap.get()) &&
                 LoadLibrary(&database_, bootstrap.get()) &&
                 (private_profile || LoadMemory(&database_, bootstrap.get())) &&
                 LoadSkills(&database_, bootstrap.get()) &&
                 LoadAssistantConfiguration(&database_, bootstrap.get())
             ? std::move(bootstrap)
             : nullptr;
}

std::optional<CoreStorageBroker::AccountReconciliationState>
CoreStorageBroker::Backend::LoadAccountReconciliationState() {
  if (!EnsureOpen()) {
    return std::nullopt;
  }
  AccountReconciliationState state;
  bool invalid_session = false;
  const std::optional<bool> pending =
      HasPendingAccountSessionMutation(&database_);
  if (!pending || !ReadCommittedAccountSession(
                      &database_, &state.committed_session, &invalid_session)) {
    return std::nullopt;
  }
  if (invalid_session) {
    if (!DeleteCommittedAccountSession(&database_)) {
      return std::nullopt;
    }
    state.committed_session.reset();
  }
  state.has_pending_session_mutation = *pending;
  return state;
}

bool CoreStorageBroker::Backend::FinishAccountReconciliation() {
  if (!EnsureOpen()) {
    return false;
  }
  sql::Transaction transaction(&database_);
  if (!transaction.Begin()) {
    return false;
  }
  sql::Statement settle(database_.GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_effect_journal SET terminal_status=? WHERE effect_id IN "
      "(SELECT effect_id FROM core_account_session_pending) AND "
      "terminal_status IS NULL"));
  settle.BindInt(0, static_cast<int>(mojom::EffectStatus::kOutcomeUnknown));
  if (!settle.Run() || !DeleteCommittedAccountSession(&database_)) {
    return false;
  }
  sql::Statement clear_pending(database_.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM core_account_session_pending"));
  return clear_pending.Run() && transaction.Commit();
}

bool CoreStorageBroker::Backend::EnsureOpen() {
  if (!database_.is_open()) {
    if (ephemeral_) {
      if (!database_.OpenInMemory()) {
        return false;
      }
    } else if (database_path_.empty() ||
               !base::CreateDirectory(database_path_.DirName()) ||
               !database_.Open(database_path_)) {
      return false;
    }
  }
  if (schema_verified_) {
    return true;
  }

  sql::Transaction transaction(&database_);
  if (!transaction.Begin() ||
      !database_.Execute(storage_schema::kLedgerStatement)) {
    return false;
  }
  std::optional<std::pair<int, std::string>> recorded_identity;
  {
    sql::Statement recorded(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT version,checksum FROM taffy_storage_schema WHERE component=?"));
    recorded.BindString(0, storage_schema::kComponent);
    if (recorded.Step()) {
      recorded_identity.emplace(recorded.ColumnInt(0),
                                recorded.ColumnString(1));
    } else if (!recorded.Succeeded()) {
      return false;
    }
  }
  if (recorded_identity) {
    const int recorded_version = recorded_identity->first;
    const std::string& recorded_checksum = recorded_identity->second;
    const bool matches =
        recorded_version == static_cast<int>(storage_schema::kVersion) &&
        recorded_checksum == storage_schema::kChecksum;
    if ((!matches && !ApplyKnownMigration(&database_, recorded_version,
                                          recorded_checksum)) ||
        !transaction.Commit()) {
      return false;
    }
    schema_verified_ = true;
    return true;
  }
  for (base::cstring_view statement : storage_schema::kStatements) {
    if (!database_.Execute(statement)) {
      return false;
    }
  }
  {
    sql::Statement insert(database_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO taffy_storage_schema(component,version,checksum) "
        "VALUES(?,?,?)"));
    insert.BindString(0, storage_schema::kComponent);
    insert.BindInt(1, static_cast<int>(storage_schema::kVersion));
    insert.BindString(2, storage_schema::kChecksum);
    if (!insert.Run()) {
      return false;
    }
  }
  if (!transaction.Commit()) {
    return false;
  }
  schema_verified_ = true;
  return true;
}

}  // namespace taffy
