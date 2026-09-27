// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_skill_projection.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "crypto/hash.h"
#include "taffy/components/storage/browser/backup_record_codec.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;

bool IsSkillKind(mojom::BackupRecordKind kind) {
  return kind == mojom::BackupRecordKind::kUserAuthoredSkill ||
         kind == mojom::BackupRecordKind::kLearnedProcedure;
}

}  // namespace

base::expected<std::vector<mojom::SkillRecordPtr>, BackupSkillProjectionError>
DecodeSelectedBackupSkillRecords(
    const std::vector<BackupSnapshotRecord>& records) {
  if (records.size() > mojom::kMaxBackupRecords) {
    return base::unexpected(BackupSkillProjectionError::kCapacityExceeded);
  }
  std::vector<mojom::SkillRecordPtr> decoded;
  std::set<std::string> skill_ids;
  for (const auto& record : records) {
    const mojom::BackupRecordDescriptor* descriptor = record.descriptor.get();
    if (!descriptor) {
      return base::unexpected(BackupSkillProjectionError::kInvalidRecord);
    }
    if (!IsSkillKind(descriptor->kind)) {
      continue;
    }
    if (decoded.size() >= mojom::kMaxSkillsPerProfile) {
      return base::unexpected(BackupSkillProjectionError::kCapacityExceeded);
    }
    if (descriptor->state != mojom::BackupRecordState::kActive ||
        descriptor->schema_version != kSkillBackupSchemaVersion ||
        descriptor->plaintext_bytes != record.plaintext.size() ||
        descriptor->plaintext_sha256.size() != crypto::hash::kSha256Size ||
        std::ranges::none_of(descriptor->plaintext_sha256,
                             [](uint8_t byte) { return byte != 0u; }) ||
        !std::ranges::equal(crypto::hash::Sha256(record.plaintext),
                            descriptor->plaintext_sha256) ||
        !skill_ids.insert(descriptor->stable_id).second) {
      return base::unexpected(BackupSkillProjectionError::kInvalidRecord);
    }
    auto skill =
        DecodeSkillRecordV1(record.plaintext, descriptor->kind,
                            descriptor->stable_id, descriptor->revision);
    if (!skill.has_value()) {
      return base::unexpected(BackupSkillProjectionError::kInvalidRecord);
    }
    decoded.push_back(std::move(*skill));
  }
  std::ranges::sort(decoded, {}, [](const auto& skill) -> const std::string& {
    return skill->skill_id;
  });
  return decoded;
}

}  // namespace taffy::storage::backup
