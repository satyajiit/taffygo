// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "base/strings/cstring_view.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_skill_shape.h"
#include "taffy/components/storage/browser/core_storage_skills.h"

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

// What the installation row says about a skill this database already holds.
struct InstalledSkill {
  std::string origin;
  int provenance = 0;
  int64_t active_version = 0;
  std::string effect_id;
};

// Reads the installation row. The outer optional separates "the query failed"
// from "there is no such skill", which are different answers and lead to
// different writes.
bool ReadInstalledSkill(sql::Database* database,
                        const std::string& skill_id,
                        std::optional<InstalledSkill>* installed) {
  installed->reset();
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT origin,provenance,active_version,effect_id "
      "FROM core_skill_installation WHERE skill_id=?"));
  query.BindString(0, skill_id);
  if (query.Step()) {
    InstalledSkill row;
    row.origin = query.ColumnString(0);
    row.provenance = query.ColumnInt(1);
    row.active_version = query.ColumnInt64(2);
    row.effect_id = query.ColumnString(3);
    installed->emplace(std::move(row));
    return true;
  }
  return query.Succeeded();
}

// Whether this exact effect already wrote this exact version row. A storage
// commit bypasses the outer effect journal, so idempotency is the writer's own
// to prove; the version row's effect_id is where it is proven.
bool VersionAlreadyWritten(sql::Database* database,
                           const std::string& effect_id,
                           const mojom::SkillInstallEffect& body,
                           std::optional<bool>* identical) {
  identical->reset();
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT skill_id,version,definition,step_count FROM core_skill_version "
      "WHERE effect_id=?"));
  query.BindString(0, effect_id);
  if (query.Step()) {
    identical->emplace(
        query.ColumnString(0) == body.skill_id &&
        query.ColumnInt64(1) == static_cast<int64_t>(body.version) &&
        query.ColumnBlobAsVector(2) == body.definition &&
        query.ColumnInt64(3) == static_cast<int64_t>(body.step_count));
    return true;
  }
  return query.Succeeded();
}

bool CountIsUnder(sql::Database* database,
                  base::cstring_view text,
                  const std::string* parameter,
                  uint64_t limit) {
  sql::Statement query(database->GetUniqueStatement(text));
  if (parameter) {
    query.BindString(0, *parameter);
  }
  if (!query.Step()) {
    return false;
  }
  const int64_t count = query.ColumnInt64(0);
  return count >= 0 && static_cast<uint64_t>(count) < limit;
}

bool WriteSkillVersion(sql::Database* database,
                       const std::string& effect_id,
                       const mojom::SkillInstallEffect& body) {
  sql::Statement insert(database->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO core_skill_version(skill_id,version,status,definition,"
      "step_count,effect_id,created_at_utc_ms) VALUES(?,?,?,?,?,?,?)"));
  insert.BindString(0, body.skill_id);
  insert.BindInt64(1, static_cast<int64_t>(body.version));
  // Every version enters at DRAFT, whatever its provenance. "The assistant did
  // this once" is evidence that it worked once and not that it is correct.
  insert.BindInt(2, static_cast<int>(mojom::SkillStatus::kDraft));
  insert.BindBlob(3, body.definition);
  insert.BindInt64(4, static_cast<int64_t>(body.step_count));
  insert.BindString(5, effect_id);
  insert.BindInt64(6, static_cast<int64_t>(body.recorded_at_utc_ms));
  return insert.Run();
}

bool SupersedeEarlierVersions(sql::Database* database,
                              const mojom::SkillInstallEffect& body) {
  sql::Statement update(database->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_skill_version SET status=? WHERE skill_id=? AND version<? "
      "AND status<>?"));
  update.BindInt(0, static_cast<int>(mojom::SkillStatus::kSuperseded));
  update.BindString(1, body.skill_id);
  update.BindInt64(2, static_cast<int64_t>(body.version));
  // A person's DISABLED is not overwritten by a later version arriving.
  // Superseding is a statement about which steps are current; disabling is a
  // person's standing instruction, and only a person may lift it.
  update.BindInt(3, static_cast<int>(mojom::SkillStatus::kDisabled));
  return update.Run();
}

bool CommitInstall(sql::Database* database,
                   const std::string& effect_id,
                   const mojom::SkillInstallEffect& body) {
  if (!skill_internal::IsSkillIdentifier(body.skill_id) ||
      !skill_internal::IsOrigin(body.origin) ||
      body.provenance == mojom::SkillProvenance::kInstalledFromPack ||
      !skill_internal::IsVersion(static_cast<int64_t>(body.version)) ||
      !skill_internal::IsDefinition(body.definition) ||
      !skill_internal::IsStepCount(static_cast<int64_t>(body.step_count)) ||
      !skill_internal::IsTimestamp(body.recorded_at_utc_ms)) {
    return false;
  }
  sql::Transaction transaction(database);
  if (!transaction.Begin()) {
    return false;
  }
  std::optional<bool> identical;
  if (!VersionAlreadyWritten(database, effect_id, body, &identical)) {
    return false;
  }
  if (identical) {
    return *identical && transaction.Commit();
  }
  std::optional<InstalledSkill> installed;
  if (!ReadInstalledSkill(database, body.skill_id, &installed)) {
    return false;
  }
  const int64_t recorded_at = static_cast<int64_t>(body.recorded_at_utc_ms);
  if (installed) {
    // A skill does not change the site it is about, and it does not change the
    // story of where it came from. Either would make the record a different
    // record still wearing the same identity.
    if (installed->origin != body.origin ||
        installed->provenance != static_cast<int>(body.provenance) ||
        installed->active_version >= static_cast<int64_t>(body.version) ||
        !CountIsUnder(
            database,
            "SELECT COUNT(*) FROM core_skill_version WHERE skill_id=?",
            &body.skill_id, mojom::kMaxSkillVersionsPerSkill)) {
      return false;
    }
    sql::Statement update(database->GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE core_skill_installation SET active_version=?,effect_id=?,"
        "updated_at_utc_ms=? WHERE skill_id=?"));
    update.BindInt64(0, static_cast<int64_t>(body.version));
    update.BindString(1, effect_id);
    update.BindInt64(2, recorded_at);
    update.BindString(3, body.skill_id);
    if (!update.Run() || database->GetLastChangeCount() != 1) {
      return false;
    }
  } else {
    if (body.version != 1u ||
        !CountIsUnder(database, "SELECT COUNT(*) FROM core_skill_installation",
                      nullptr, mojom::kMaxSkillsPerProfile)) {
      return false;
    }
    sql::Statement insert(database->GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT INTO core_skill_installation(skill_id,origin,provenance,"
        "active_version,effect_id,installed_at_utc_ms,updated_at_utc_ms) "
        "VALUES(?,?,?,?,?,?,?)"));
    insert.BindString(0, body.skill_id);
    insert.BindString(1, body.origin);
    insert.BindInt(2, static_cast<int>(body.provenance));
    insert.BindInt64(3, static_cast<int64_t>(body.version));
    insert.BindString(4, effect_id);
    insert.BindInt64(5, recorded_at);
    insert.BindInt64(6, recorded_at);
    if (!insert.Run()) {
      return false;
    }
  }
  return WriteSkillVersion(database, effect_id, body) &&
         SupersedeEarlierVersions(database, body) && transaction.Commit();
}

bool CommitStatus(sql::Database* database,
                  const std::string& effect_id,
                  const mojom::SkillStatusEffect& body) {
  if (!skill_internal::IsSkillIdentifier(body.skill_id) ||
      !skill_internal::IsVersion(static_cast<int64_t>(body.version)) ||
      !skill_internal::IsTimestamp(body.changed_at_utc_ms)) {
    return false;
  }
  sql::Transaction transaction(database);
  if (!transaction.Begin()) {
    return false;
  }
  std::optional<InstalledSkill> installed;
  if (!ReadInstalledSkill(database, body.skill_id, &installed) || !installed) {
    return false;
  }
  if (installed->effect_id == effect_id) {
    return transaction.Commit();
  }
  // A move decided against one version must not land on another. Superseding
  // mints a new version, so a status effect in flight while that happens names
  // a version that is no longer current, and is refused rather than retargeted.
  if (installed->active_version != static_cast<int64_t>(body.version)) {
    return false;
  }
  sql::Statement move(database->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_skill_version SET status=? WHERE skill_id=? AND version=?"));
  move.BindInt(0, static_cast<int>(body.status));
  move.BindString(1, body.skill_id);
  move.BindInt64(2, static_cast<int64_t>(body.version));
  if (!move.Run() || database->GetLastChangeCount() != 1) {
    return false;
  }
  sql::Statement touch(database->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE core_skill_installation SET effect_id=?,updated_at_utc_ms=? "
      "WHERE skill_id=?"));
  touch.BindString(0, effect_id);
  touch.BindInt64(1, static_cast<int64_t>(body.changed_at_utc_ms));
  touch.BindString(2, body.skill_id);
  return touch.Run() && database->GetLastChangeCount() == 1 &&
         transaction.Commit();
}

bool CommitRun(sql::Database* database,
               const std::string& effect_id,
               const mojom::SkillRunEffect& body) {
  if (!skill_internal::IsSkillIdentifier(body.skill_id) ||
      !skill_internal::IsVersion(static_cast<int64_t>(body.version)) ||
      body.task_id.empty() ||
      body.task_id.size() > mojom::kMaxIdentifierBytes ||
      !skill_internal::IsTimestamp(body.ran_at_utc_ms)) {
    return false;
  }
  sql::Transaction transaction(database);
  if (!transaction.Begin()) {
    return false;
  }
  sql::Statement append(database->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR IGNORE INTO core_skill_run(effect_id,skill_id,version,task_id,"
      "outcome,ran_at_utc_ms) SELECT ?,?,?,?,?,? FROM core_skill_version "
      "WHERE skill_id=? AND version=?"));
  append.BindString(0, effect_id);
  append.BindString(1, body.skill_id);
  append.BindInt64(2, static_cast<int64_t>(body.version));
  append.BindString(3, body.task_id);
  append.BindInt(4, static_cast<int>(body.outcome));
  append.BindInt64(5, static_cast<int64_t>(body.ran_at_utc_ms));
  append.BindString(6, body.skill_id);
  append.BindInt64(7, static_cast<int64_t>(body.version));
  if (!append.Run()) {
    return false;
  }
  // Either this effect appended its row now, or an earlier delivery of the
  // same effect already did. Both are success; a run recorded twice would put
  // one replay into recall twice and shift what recency means. Anything else
  // is a refusal: either the version named does not exist, or a different run
  // is already wearing this effect's identity.
  if (database->GetLastChangeCount() == 1) {
    return transaction.Commit();
  }
  sql::Statement present(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT skill_id,version,task_id,outcome,ran_at_utc_ms "
      "FROM core_skill_run WHERE effect_id=?"));
  present.BindString(0, effect_id);
  if (!present.Step()) {
    return false;
  }
  const bool identical =
      present.ColumnString(0) == body.skill_id &&
      present.ColumnInt64(1) == static_cast<int64_t>(body.version) &&
      present.ColumnString(2) == body.task_id &&
      present.ColumnInt(3) == static_cast<int>(body.outcome) &&
      present.ColumnInt64(4) == static_cast<int64_t>(body.ran_at_utc_ms);
  return identical && transaction.Commit();
}

bool CommitForget(sql::Database* database,
                  const mojom::SkillForgetEffect& body) {
  if (!skill_internal::IsSkillIdentifier(body.skill_id)) {
    return false;
  }
  sql::Transaction transaction(database);
  if (!transaction.Begin()) {
    return false;
  }
  // Runs and versions before the installation they name, so nothing is ever
  // left pointing at a skill that is gone. Re-delivery finds nothing to delete
  // and succeeds: being rid of a skill twice is being rid of it.
  static constexpr char kRemoveRuns[] =
      "DELETE FROM core_skill_run WHERE skill_id=?";
  static constexpr char kRemoveVersions[] =
      "DELETE FROM core_skill_version WHERE skill_id=?";
  static constexpr char kRemoveInstallation[] =
      "DELETE FROM core_skill_installation WHERE skill_id=?";
  // The element type is named because the three constants are arrays of
  // different lengths: left to deduce, they decay to `const char*`, which
  // `base::cstring_view` refuses on purpose - a pointer carries no promise
  // that a terminator is there. Naming it converts each array on its own.
  for (base::cstring_view text : std::initializer_list<base::cstring_view>{
           kRemoveRuns, kRemoveVersions, kRemoveInstallation}) {
    sql::Statement remove(database->GetUniqueStatement(text));
    remove.BindString(0, body.skill_id);
    if (!remove.Run()) {
      return false;
    }
  }
  return transaction.Commit();
}

}  // namespace

bool CommitSkillOperation(sql::Database* database,
                          const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  if (!skill_internal::HasNoTaskFields(body) ||
      !skill_internal::HasOnlySkillBody(body, body.operation_kind) ||
      effect.effect_id.empty() ||
      effect.effect_id.size() > mojom::kMaxIdentifierBytes) {
    return false;
  }
  switch (body.operation_kind) {
    case mojom::StorageOperation::kInstallSkill:
      return CommitInstall(database, effect.effect_id, *body.install_skill);
    case mojom::StorageOperation::kSetSkillStatus:
      return CommitStatus(database, effect.effect_id, *body.skill_status);
    case mojom::StorageOperation::kRecordSkillRun:
      return CommitRun(database, effect.effect_id, *body.skill_run);
    case mojom::StorageOperation::kForgetSkill:
      return CommitForget(database, *body.forget_skill);
    case mojom::StorageOperation::kAppendTaskCommit:
    case mojom::StorageOperation::kQueryWorkspace:
    case mojom::StorageOperation::kDeleteSource:
    case mojom::StorageOperation::kUpsertWorkspace:
    case mojom::StorageOperation::kSetAssistantConfiguration:
    case mojom::StorageOperation::kDeleteWorkspace:
    case mojom::StorageOperation::kUpsertLibraryEntry:
    case mojom::StorageOperation::kRemoveLibraryEntry:
    case mojom::StorageOperation::kUpsertMemory:
    case mojom::StorageOperation::kDeleteMemory:
      return false;
  }
  return false;
}

}  // namespace taffy
