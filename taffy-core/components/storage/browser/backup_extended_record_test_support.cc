// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_extended_record_test_support.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crypto/hash.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_test_support.h"

namespace taffy::storage::backup::test {
namespace {

namespace mojom = core_service::mojom;

void LittleU32(std::vector<uint8_t>* output, uint32_t value) {
  for (uint32_t index = 0u; index < 4u; ++index) {
    output->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void LittleU64(std::vector<uint8_t>* output, uint64_t value) {
  for (uint32_t index = 0u; index < 8u; ++index) {
    output->push_back(static_cast<uint8_t>(value >> (index * 8u)));
  }
}

void LittleText(std::vector<uint8_t>* output, std::string_view value) {
  LittleU32(output, static_cast<uint32_t>(value.size()));
  output->insert(output->end(), value.begin(), value.end());
}

void BigU16(std::vector<uint8_t>* output, uint16_t value) {
  output->push_back(static_cast<uint8_t>(value >> 8u));
  output->push_back(static_cast<uint8_t>(value));
}

void BigU32(std::vector<uint8_t>* output, uint32_t value) {
  for (uint32_t index = 0u; index < 4u; ++index) {
    output->push_back(static_cast<uint8_t>(value >> ((3u - index) * 8u)));
  }
}

void BigText(std::vector<uint8_t>* output, std::string_view value) {
  BigU16(output, static_cast<uint16_t>(value.size()));
  output->insert(output->end(), value.begin(), value.end());
}

std::string_view StatusLabel(mojom::SkillStatus status) {
  switch (status) {
    case mojom::SkillStatus::kDraft:
      return "draft";
    case mojom::SkillStatus::kActive:
      return "active";
    case mojom::SkillStatus::kSuperseded:
      return "superseded";
    case mojom::SkillStatus::kRetired:
      return "retired";
    case mojom::SkillStatus::kDisabled:
      return "disabled";
    default:
      return {};
  }
}

std::vector<uint8_t> Definition(std::string_view skill_id,
                                uint32_t version,
                                std::string_view provenance,
                                mojom::SkillStatus status) {
  std::vector<uint8_t> bytes = {'T', 'F', 'S', 'K'};
  BigU16(&bytes, 1u);
  BigU16(&bytes, 1u);
  BigText(&bytes, skill_id);
  BigU32(&bytes, version);
  BigText(&bytes, StatusLabel(status));
  BigText(&bytes, provenance);
  BigText(&bytes, "https://example.test");
  BigU16(&bytes, 1u);
  BigText(&bytes, "role_present");
  BigText(&bytes, "SEARCH_FIELD");
  BigText(&bytes, "browser.dom.query");
  BigText(&bytes, "NO_MUTATION");
  bytes.push_back(0u);
  BigU16(&bytes, 0u);
  return bytes;
}

void AppendEntry(DormantBackupRestoreScenario* scenario,
                 mojom::BackupRecordKind kind,
                 std::string stable_id,
                 uint64_t revision,
                 std::vector<uint8_t> plaintext,
                 mojom::BackupRecordState state) {
  auto entry = mojom::BackupRestorePlanEntry::New();
  entry->kind = kind;
  entry->stable_id = std::move(stable_id);
  entry->archive_revision = revision;
  entry->action = state == mojom::BackupRecordState::kTombstone
                      ? mojom::BackupRestoreAction::kStageDeletion
                      : mojom::BackupRestoreAction::kStageCreate;
  entry->schema_version = 1u;
  entry->state = state;
  entry->plaintext_bytes = plaintext.size();
  if (state == mojom::BackupRecordState::kTombstone) {
    entry->plaintext_sha256.assign(32u, 0u);
  } else {
    const auto digest = crypto::hash::Sha256(plaintext);
    entry->plaintext_sha256.assign(digest.begin(), digest.end());
  }
  scenario->payload.insert(scenario->payload.end(), plaintext.begin(),
                           plaintext.end());
  scenario->plan->entries.push_back(std::move(entry));
}

}  // namespace

mojom::WorkspaceRestoreRecordPtr SavedWorkspace(std::string_view workspace_id,
                                                uint64_t revision,
                                                bool saved) {
  std::vector<uint8_t> snapshot = {'T', 'A', 'F', 'F', 'Y', 'W', 'S', '1'};
  LittleU32(&snapshot, 5u);
  LittleText(&snapshot, workspace_id);
  LittleU64(&snapshot, revision);
  LittleText(&snapshot, "Compare research");
  LittleText(&snapshot, "Saved research");
  snapshot.push_back(3u);  // Done.
  snapshot.push_back(static_cast<uint8_t>(saved));
  LittleU64(&snapshot, 2'000u);
  snapshot.push_back(2u);  // BuildSourceTable.
  LittleU32(&snapshot, 0u);
  LittleU32(&snapshot, 0u);
  return mojom::WorkspaceRestoreRecord::New(std::string(workspace_id), revision,
                                            std::move(snapshot));
}

mojom::SkillRecordPtr CurrentProcedure(std::string_view skill_id,
                                       uint32_t version,
                                       mojom::BackupRecordKind kind,
                                       mojom::SkillStatus status) {
  const bool authored = kind == mojom::BackupRecordKind::kUserAuthoredSkill;
  auto record = mojom::SkillRecord::New();
  record->skill_id = skill_id;
  record->origin = "https://example.test";
  record->provenance = authored ? mojom::SkillProvenance::kAuthored
                                : mojom::SkillProvenance::kRecordedFromTask;
  record->status = status;
  record->active_version = version;
  record->definition = Definition(
      skill_id, version, authored ? "authored" : "recorded_from_task", status);
  record->step_count = 1u;
  record->installed_at_utc_ms = 1'000u;
  record->updated_at_utc_ms = 2'000u;
  return record;
}

bool AppendExtendedRestoreRecords(DormantBackupRestoreScenario* scenario) {
  if (!scenario || !scenario->plan) {
    return false;
  }
  auto workspace =
      EncodeSavedWorkspaceRecordV1(*SavedWorkspace(kExtendedWorkspaceId, 7u));
  auto authored = EncodeSkillRecordV1(
      *CurrentProcedure(kExtendedAuthoredSkillId, 2u,
                        mojom::BackupRecordKind::kUserAuthoredSkill,
                        mojom::SkillStatus::kRetired),
      mojom::BackupRecordKind::kUserAuthoredSkill);
  auto learned = EncodeSkillRecordV1(
      *CurrentProcedure(kExtendedLearnedProcedureId, 3u,
                        mojom::BackupRecordKind::kLearnedProcedure,
                        mojom::SkillStatus::kDisabled),
      mojom::BackupRecordKind::kLearnedProcedure);
  if (!workspace || !authored || !learned) {
    return false;
  }
  AppendEntry(scenario, mojom::BackupRecordKind::kSavedWorkspace,
              kExtendedWorkspaceId, 7u, std::move(*workspace),
              mojom::BackupRecordState::kActive);
  AppendEntry(scenario, mojom::BackupRecordKind::kSavedWorkspace,
              kExtendedDeletedWorkspaceId, 3u, {},
              mojom::BackupRecordState::kTombstone);
  AppendEntry(scenario, mojom::BackupRecordKind::kUserAuthoredSkill,
              kExtendedAuthoredSkillId, 2u, std::move(*authored),
              mojom::BackupRecordState::kActive);
  AppendEntry(scenario, mojom::BackupRecordKind::kLearnedProcedure,
              kExtendedLearnedProcedureId, 3u, std::move(*learned),
              mojom::BackupRecordState::kActive);
  return true;
}

}  // namespace taffy::storage::backup::test
