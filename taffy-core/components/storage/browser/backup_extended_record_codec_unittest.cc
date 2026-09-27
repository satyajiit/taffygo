// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/numerics/byte_conversions.h"
#include "crypto/hash.h"
#include "taffy/components/storage/browser/backup_extended_record_test_support.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_record_codec_internal.h"
#include "taffy/components/storage/browser/backup_skill_projection.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
namespace test_support = test;
using codec_internal::RecordTag;

constexpr char kWorkspaceId[] = "11111111111111111111111111111111";
constexpr char kAuthoredId[] = "compare-products";
constexpr char kLearnedId[] = "capture-product-details";

uint32_t EncodedTag(base::span<const uint8_t> bytes) {
  if (bytes.size() < 16u) {
    return std::numeric_limits<uint32_t>::max();
  }
  return base::U32FromLittleEndian(bytes.subspan<12u, 4u>());
}

BackupSnapshotRecord SkillSnapshot(std::string_view skill_id,
                                   uint32_t version,
                                   mojom::BackupRecordKind kind,
                                   mojom::SkillStatus status) {
  auto skill = test_support::CurrentProcedure(skill_id, version, kind, status);
  auto encoded = EncodeSkillRecordV1(*skill, kind);
  EXPECT_TRUE(encoded.has_value());
  BackupSnapshotRecord record;
  if (!encoded) {
    return record;
  }
  record.plaintext = std::move(*encoded);
  record.descriptor = mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = skill_id;
  record.descriptor->revision = version;
  record.descriptor->schema_version = kSkillBackupSchemaVersion;
  record.descriptor->state = mojom::BackupRecordState::kActive;
  record.descriptor->plaintext_bytes = record.plaintext.size();
  const auto digest = crypto::hash::Sha256(record.plaintext);
  record.descriptor->plaintext_sha256.assign(digest.begin(), digest.end());
  return record;
}

TEST(BackupExtendedRecordCodecTest,
     FrozenTagsAndTypedPayloadsRoundTripExactly) {
  static_assert(static_cast<uint32_t>(RecordTag::kSavedWorkspace) == 1u);
  static_assert(static_cast<uint32_t>(RecordTag::kUserAuthoredSkill) == 4u);
  static_assert(static_cast<uint32_t>(RecordTag::kLearnedProcedure) == 5u);

  auto workspace = test_support::SavedWorkspace(kWorkspaceId, 7u);
  auto authored = test_support::CurrentProcedure(
      kAuthoredId, 2u, mojom::BackupRecordKind::kUserAuthoredSkill,
      mojom::SkillStatus::kActive);
  auto learned = test_support::CurrentProcedure(
      kLearnedId, 3u, mojom::BackupRecordKind::kLearnedProcedure,
      mojom::SkillStatus::kDisabled);
  auto workspace_bytes = EncodeSavedWorkspaceRecordV1(*workspace);
  auto authored_bytes = EncodeSkillRecordV1(
      *authored, mojom::BackupRecordKind::kUserAuthoredSkill);
  auto learned_bytes =
      EncodeSkillRecordV1(*learned, mojom::BackupRecordKind::kLearnedProcedure);

  ASSERT_TRUE(workspace_bytes.has_value());
  ASSERT_TRUE(authored_bytes.has_value());
  ASSERT_TRUE(learned_bytes.has_value());
  EXPECT_EQ(EncodedTag(*workspace_bytes), 1u);
  EXPECT_EQ(EncodedTag(*authored_bytes), 4u);
  EXPECT_EQ(EncodedTag(*learned_bytes), 5u);

  auto opened_workspace =
      DecodeSavedWorkspaceRecordV1(*workspace_bytes, kWorkspaceId, 7u);
  auto opened_authored = DecodeSkillRecordV1(
      *authored_bytes, mojom::BackupRecordKind::kUserAuthoredSkill, kAuthoredId,
      2u);
  auto opened_learned = DecodeSkillRecordV1(
      *learned_bytes, mojom::BackupRecordKind::kLearnedProcedure, kLearnedId,
      3u);
  ASSERT_TRUE(opened_workspace.has_value());
  ASSERT_TRUE(opened_authored.has_value());
  ASSERT_TRUE(opened_learned.has_value());
  EXPECT_TRUE((*opened_workspace)->Equals(*workspace));
  EXPECT_TRUE((*opened_authored)->Equals(*authored));
  EXPECT_TRUE((*opened_learned)->Equals(*learned));
}

TEST(BackupExtendedRecordCodecTest,
     UnsavedWorkspaceAndDescriptorSubstitutionAreRefused) {
  auto unsaved = test_support::SavedWorkspace(kWorkspaceId, 7u, false);
  EXPECT_EQ(EncodeSavedWorkspaceRecordV1(*unsaved).error(),
            BackupRecordCodecError::kInvalidRecord);

  auto saved = test_support::SavedWorkspace(kWorkspaceId, 7u);
  auto bytes = EncodeSavedWorkspaceRecordV1(*saved);
  ASSERT_TRUE(bytes.has_value());
  EXPECT_EQ(DecodeSavedWorkspaceRecordV1(*bytes,
                                         "22222222222222222222222222222222", 7u)
                .error(),
            BackupRecordCodecError::kDescriptorMismatch);
  EXPECT_EQ(DecodeSavedWorkspaceRecordV1(*bytes, kWorkspaceId, 8u).error(),
            BackupRecordCodecError::kDescriptorMismatch);
}

TEST(BackupExtendedRecordCodecTest,
     SkillKindProvenanceAndDefinitionEnvelopeStayExactlyBound) {
  auto authored = test_support::CurrentProcedure(
      kAuthoredId, 2u, mojom::BackupRecordKind::kUserAuthoredSkill);
  auto bytes = EncodeSkillRecordV1(*authored,
                                   mojom::BackupRecordKind::kUserAuthoredSkill);
  ASSERT_TRUE(bytes.has_value());

  EXPECT_FALSE(DecodeSkillRecordV1(*bytes,
                                   mojom::BackupRecordKind::kLearnedProcedure,
                                   kAuthoredId, 2u)
                   .has_value());
  EXPECT_EQ(
      DecodeSkillRecordV1(*bytes, mojom::BackupRecordKind::kUserAuthoredSkill,
                          kLearnedId, 2u)
          .error(),
      BackupRecordCodecError::kDescriptorMismatch);

  authored->provenance = mojom::SkillProvenance::kRecordedFromTask;
  EXPECT_EQ(EncodeSkillRecordV1(*authored,
                                mojom::BackupRecordKind::kUserAuthoredSkill)
                .error(),
            BackupRecordCodecError::kInvalidRecord);
  authored = test_support::CurrentProcedure(
      kAuthoredId, 2u, mojom::BackupRecordKind::kUserAuthoredSkill);
  authored->definition.front() ^= 1u;
  EXPECT_EQ(EncodeSkillRecordV1(*authored,
                                mojom::BackupRecordKind::kUserAuthoredSkill)
                .error(),
            BackupRecordCodecError::kInvalidRecord);
  authored = test_support::CurrentProcedure(
      kAuthoredId, 2u, mojom::BackupRecordKind::kUserAuthoredSkill);
  authored->status = static_cast<mojom::SkillStatus>(255u);
  EXPECT_EQ(EncodeSkillRecordV1(*authored,
                                mojom::BackupRecordKind::kUserAuthoredSkill)
                .error(),
            BackupRecordCodecError::kInvalidRecord);
}

TEST(BackupExtendedRecordCodecTest,
     EveryTruncatedPrefixAndTrailingByteIsRefused) {
  auto workspace = EncodeSavedWorkspaceRecordV1(
      *test_support::SavedWorkspace(kWorkspaceId, 7u));
  auto procedure = EncodeSkillRecordV1(
      *test_support::CurrentProcedure(
          kLearnedId, 3u, mojom::BackupRecordKind::kLearnedProcedure),
      mojom::BackupRecordKind::kLearnedProcedure);
  ASSERT_TRUE(workspace.has_value());
  ASSERT_TRUE(procedure.has_value());
  for (size_t size = 0u; size < workspace->size(); ++size) {
    EXPECT_FALSE(DecodeSavedWorkspaceRecordV1(
                     base::span(*workspace).first(size), kWorkspaceId, 7u)
                     .has_value());
  }
  for (size_t size = 0u; size < procedure->size(); ++size) {
    EXPECT_FALSE(DecodeSkillRecordV1(base::span(*procedure).first(size),
                                     mojom::BackupRecordKind::kLearnedProcedure,
                                     kLearnedId, 3u)
                     .has_value());
  }
  workspace->push_back(0u);
  procedure->push_back(0u);
  EXPECT_FALSE(
      DecodeSavedWorkspaceRecordV1(*workspace, kWorkspaceId, 7u).has_value());
  EXPECT_FALSE(DecodeSkillRecordV1(*procedure,
                                   mojom::BackupRecordKind::kLearnedProcedure,
                                   kLearnedId, 3u)
                   .has_value());
}

TEST(BackupExtendedRecordCodecTest,
     SelectedSkillProjectionIsCanonicalBoundedAndKeepsExactStatus) {
  std::vector<BackupSnapshotRecord> records;
  records.push_back(SkillSnapshot("z-learned", 3u,
                                  mojom::BackupRecordKind::kLearnedProcedure,
                                  mojom::SkillStatus::kDisabled));
  records.push_back(SkillSnapshot("a-authored", 2u,
                                  mojom::BackupRecordKind::kUserAuthoredSkill,
                                  mojom::SkillStatus::kRetired));
  auto skills = DecodeSelectedBackupSkillRecords(records);
  ASSERT_TRUE(skills.has_value());
  ASSERT_EQ(skills->size(), 2u);
  EXPECT_EQ((*skills)[0]->skill_id, "a-authored");
  EXPECT_EQ((*skills)[0]->status, mojom::SkillStatus::kRetired);
  EXPECT_EQ((*skills)[1]->skill_id, "z-learned");
  EXPECT_EQ((*skills)[1]->status, mojom::SkillStatus::kDisabled);

  std::vector<BackupSnapshotRecord> duplicate;
  duplicate.push_back(SkillSnapshot("same-skill", 1u,
                                    mojom::BackupRecordKind::kUserAuthoredSkill,
                                    mojom::SkillStatus::kDraft));
  duplicate.push_back(SkillSnapshot("same-skill", 1u,
                                    mojom::BackupRecordKind::kLearnedProcedure,
                                    mojom::SkillStatus::kDraft));
  auto refused = DecodeSelectedBackupSkillRecords(duplicate);
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error(), BackupSkillProjectionError::kInvalidRecord);
}

}  // namespace
}  // namespace taffy::storage::backup
