// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_skills.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "base/strings/cstring_view.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_skill_shape.h"
#include "taffy/components/storage/browser/core_storage_workspace.h"

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

std::optional<mojom::SkillProvenance> ProvenanceFromStorage(int value) {
  switch (value) {
    case static_cast<int>(mojom::SkillProvenance::kAuthored):
      return mojom::SkillProvenance::kAuthored;
    case static_cast<int>(mojom::SkillProvenance::kRecordedFromTask):
      return mojom::SkillProvenance::kRecordedFromTask;
    // A pack this build cannot verify is a pack this build must not restore.
    // Who signs one is OD-107, and nothing writes the member, so a row that
    // carries it was not written by this product.
    case static_cast<int>(mojom::SkillProvenance::kInstalledFromPack):
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

std::optional<mojom::SkillStatus> StatusFromStorage(int value) {
  switch (value) {
    case static_cast<int>(mojom::SkillStatus::kDraft):
      return mojom::SkillStatus::kDraft;
    case static_cast<int>(mojom::SkillStatus::kActive):
      return mojom::SkillStatus::kActive;
    case static_cast<int>(mojom::SkillStatus::kSuperseded):
      return mojom::SkillStatus::kSuperseded;
    case static_cast<int>(mojom::SkillStatus::kRetired):
      return mojom::SkillStatus::kRetired;
    case static_cast<int>(mojom::SkillStatus::kDisabled):
      return mojom::SkillStatus::kDisabled;
    default:
      return std::nullopt;
  }
}

std::optional<mojom::SkillRunOutcome> OutcomeFromStorage(int value) {
  switch (value) {
    case static_cast<int>(mojom::SkillRunOutcome::kCompleted):
      return mojom::SkillRunOutcome::kCompleted;
    case static_cast<int>(mojom::SkillRunOutcome::kRefused):
      return mojom::SkillRunOutcome::kRefused;
    case static_cast<int>(mojom::SkillRunOutcome::kAbandoned):
      return mojom::SkillRunOutcome::kAbandoned;
    case static_cast<int>(mojom::SkillRunOutcome::kUnavailable):
      return mojom::SkillRunOutcome::kUnavailable;
    default:
      return std::nullopt;
  }
}

bool LoadInstalledSkills(sql::Database* database,
                         mojom::CoreBootstrap* bootstrap) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT i.skill_id,i.origin,i.provenance,v.status,i.active_version,"
      "v.definition,v.step_count,i.installed_at_utc_ms,i.updated_at_utc_ms "
      "FROM core_skill_installation i JOIN core_skill_version v "
      "ON v.skill_id=i.skill_id AND v.version=i.active_version "
      "ORDER BY i.skill_id"));
  while (query.Step()) {
    if (bootstrap->skills.size() >= mojom::kMaxSkillsPerProfile) {
      return false;
    }
    auto record = mojom::SkillRecord::New();
    record->skill_id = query.ColumnString(0);
    record->origin = query.ColumnString(1);
    const std::optional<mojom::SkillProvenance> provenance =
        ProvenanceFromStorage(query.ColumnInt(2));
    const std::optional<mojom::SkillStatus> status =
        StatusFromStorage(query.ColumnInt(3));
    const int64_t active_version = query.ColumnInt64(4);
    record->definition = query.ColumnBlobAsVector(5);
    const int64_t step_count = query.ColumnInt64(6);
    const int64_t installed_at = query.ColumnInt64(7);
    const int64_t updated_at = query.ColumnInt64(8);
    if (!skill_internal::IsSkillIdentifier(record->skill_id) ||
        !skill_internal::IsOrigin(record->origin) || !provenance || !status ||
        !skill_internal::IsVersion(active_version) ||
        !skill_internal::IsDefinition(record->definition) ||
        !skill_internal::IsStepCount(step_count) || installed_at < 0 ||
        updated_at < 0) {
      return false;
    }
    record->provenance = *provenance;
    record->status = *status;
    record->active_version = static_cast<uint32_t>(active_version);
    record->step_count = static_cast<uint32_t>(step_count);
    record->installed_at_utc_ms = static_cast<uint64_t>(installed_at);
    record->updated_at_utc_ms = static_cast<uint64_t>(updated_at);
    bootstrap->skills.push_back(std::move(record));
  }
  return query.Succeeded();
}

// Recall, and the whole of it: the most recent runs, newest first, bounded by
// the contract's own limit. Recency is the ranking because recency is the only
// ranking this store can answer honestly (OD-104).
bool LoadRecall(sql::Database* database, mojom::CoreBootstrap* bootstrap) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT skill_id,version,task_id,outcome,ran_at_utc_ms "
      "FROM core_skill_run ORDER BY ran_at_utc_ms DESC,sequence DESC LIMIT ?"));
  query.BindInt64(0, static_cast<int64_t>(mojom::kMaxSkillRecallEntries));
  while (query.Step()) {
    auto record = mojom::SkillRunRecord::New();
    record->skill_id = query.ColumnString(0);
    const int64_t version = query.ColumnInt64(1);
    record->task_id = query.ColumnString(2);
    const std::optional<mojom::SkillRunOutcome> outcome =
        OutcomeFromStorage(query.ColumnInt(3));
    const int64_t ran_at = query.ColumnInt64(4);
    if (!skill_internal::IsSkillIdentifier(record->skill_id) ||
        !skill_internal::IsVersion(version) || record->task_id.empty() ||
        record->task_id.size() > mojom::kMaxIdentifierBytes || !outcome ||
        ran_at < 0) {
      return false;
    }
    record->version = static_cast<uint32_t>(version);
    record->outcome = *outcome;
    record->ran_at_utc_ms = static_cast<uint64_t>(ran_at);
    bootstrap->recall.push_back(std::move(record));
  }
  return query.Succeeded();
}

// Whether the restore saw every installation the database holds. The join in
// LoadInstalledSkills drops an installation whose active version has no row,
// and a skill that quietly disappears is worse than one that fails loudly: a
// person believes they still have a standing arrangement that is not there.
bool EveryInstallationWasRead(sql::Database* database, size_t read) {
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE, "SELECT COUNT(*) FROM core_skill_installation"));
  if (!query.Step()) {
    return false;
  }
  const int64_t installed = query.ColumnInt64(0);
  return installed >= 0 && static_cast<uint64_t>(installed) == read;
}

}  // namespace

bool LoadSkills(sql::Database* database, mojom::CoreBootstrap* bootstrap) {
  return LoadInstalledSkills(database, bootstrap) &&
         EveryInstallationWasRead(database, bootstrap->skills.size()) &&
         LoadRecall(database, bootstrap);
}

bool RemoveSkillsForSite(sql::Database* database, const std::string& origin) {
  if (!skill_internal::IsOrigin(origin)) {
    return false;
  }
  // Runs and versions go before the installations they name, so no row ever
  // outlives the skill it belongs to, even for the width of this statement
  // list. The origin is compared as one whole value: a skill's scope is a
  // canonical tuple origin and so is this, which is what keeps two spellings
  // of one site from becoming two sites.
  static constexpr char kRemoveRuns[] =
      "DELETE FROM core_skill_run WHERE skill_id IN "
      "(SELECT skill_id FROM core_skill_installation WHERE origin=?)";
  static constexpr char kRemoveVersions[] =
      "DELETE FROM core_skill_version WHERE skill_id IN "
      "(SELECT skill_id FROM core_skill_installation WHERE origin=?)";
  static constexpr char kRemoveInstallations[] =
      "DELETE FROM core_skill_installation WHERE origin=?";
  // Naming the element type is load-bearing: the three constants are arrays of
  // different lengths, so deduction decays them to `const char*` and
  // `base::cstring_view` has no conversion from one.
  for (base::cstring_view text : std::initializer_list<base::cstring_view>{
           kRemoveRuns, kRemoveVersions, kRemoveInstallations}) {
    sql::Statement remove(database->GetUniqueStatement(text));
    remove.BindString(0, origin);
    if (!remove.Run()) {
      return false;
    }
  }
  return true;
}

bool CommitSourceDeletion(sql::Database* database,
                          const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  if (body.operation_kind != mojom::StorageOperation::kDeleteSource ||
      !body.source_deletion || !body.workspace || body.workspace_deletion ||
      body.library_entry || body.library_deletion ||
      !skill_internal::HasNoTaskFields(body) ||
      !skill_internal::IsOrigin(body.source_deletion->origin) ||
      body.source_deletion->source_id.empty() ||
      body.source_deletion->source_id.size() > mojom::kMaxIdentifierBytes) {
    return false;
  }
  sql::Transaction transaction(database);
  // One transaction, both halves. A skill that outlived the source it was
  // recorded against is a skill about a site the person asked to be forgotten,
  // and two transactions leave a window in which exactly that is true -- a
  // window a crash can make permanent.
  return transaction.Begin() &&
         RemoveSkillsForSite(database, body.source_deletion->origin) &&
         WriteWorkspaceSnapshot(database, effect) && transaction.Commit();
}

}  // namespace taffy
