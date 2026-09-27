// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"
#include "taffy/components/storage/browser/backup_extended_record_test_support.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_skill_projection.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
constexpr auto kKind = mojom::BackupRecordKind::kLearnedProcedure;
constexpr char kSkillId[] = "learned.download";

// Actual procedure-engine::record_procedure -> encode output, also decoded
// and structurally validated by that crate. One public navigation, still a
// draft, with and without from_task("task-completed-1"). The native test uses
// frozen bytes rather than a second encoder that could agree with its reader.
constexpr char kWithoutTask[] =
    "5446534b0002000100106c6561726e65642e646f776e6c6f61640000000100056472616674"
    "00127265636f726465645f66726f6d5f7461736b00001468747470733a2f2f6578616d706c"
    "652e746573740001000c726f6c655f70726573656e740008444f43554d454e54001062726f"
    "777365722e6e617669676174650014434f4d4d49545445445f4e415649474154494f4e0000"
    "0100076164647265737300076c69746572616c000761646472657373001e68747470733a2f"
    "2f6578616d706c652e746573742f646f63756d656e7473";
constexpr char kWithTask[] =
    "5446534b0002000100106c6561726e65642e646f776e6c6f61640000000100056472616674"
    "00127265636f726465645f66726f6d5f7461736b0100107461736b2d636f6d706c65746564"
    "2d31001468747470733a2f2f6578616d706c652e746573740001000c726f6c655f70726573"
    "656e740008444f43554d454e54001062726f777365722e6e617669676174650014434f4d4d"
    "49545445445f4e415649474154494f4e00000100076164647265737300076c69746572616c"
    "000761646472657373001e68747470733a2f2f6578616d706c652e746573742f646f63756d"
    "656e7473";

mojom::SkillRecordPtr RecordedSkill(bool associated = true) {
  auto skill = mojom::SkillRecord::New();
  skill->skill_id = kSkillId;
  skill->origin = "https://example.test";
  skill->provenance = mojom::SkillProvenance::kRecordedFromTask;
  skill->status = mojom::SkillStatus::kActive;
  skill->active_version = 1u;
  skill->step_count = 1u;
  skill->installed_at_utc_ms = 100u;
  skill->updated_at_utc_ms = 200u;
  EXPECT_TRUE(base::HexStringToBytes(associated ? kWithTask : kWithoutTask,
                                    &skill->definition));
  return skill;
}

BackupSnapshotRecord Snapshot(const mojom::SkillRecord& skill) {
  BackupSnapshotRecord snapshot;
  auto encoded = EncodeSkillRecordV1(skill, kKind);
  EXPECT_TRUE(encoded.has_value());
  if (!encoded) {
    return snapshot;
  }
  snapshot.plaintext = std::move(*encoded);
  snapshot.descriptor = mojom::BackupRecordDescriptor::New();
  auto& descriptor = *snapshot.descriptor;
  descriptor.kind = kKind;
  descriptor.stable_id = skill.skill_id;
  descriptor.revision = skill.active_version;
  descriptor.schema_version = kSkillBackupSchemaVersion;
  descriptor.state = mojom::BackupRecordState::kActive;
  descriptor.plaintext_bytes = snapshot.plaintext.size();
  const auto digest = crypto::hash::Sha256(snapshot.plaintext);
  descriptor.plaintext_sha256.assign(digest.begin(), digest.end());
  return snapshot;
}

void ExpectExactRoundTrip(const mojom::SkillRecord& skill) {
  const auto original = skill.definition;
  std::vector<BackupSnapshotRecord> records;
  records.push_back(Snapshot(skill));
  ASSERT_TRUE(records.front().descriptor);
  auto decoded = DecodeSelectedBackupSkillRecords(records);
  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(1u, decoded->size());
  EXPECT_TRUE((*decoded)[0]->Equals(skill));
  EXPECT_EQ(original, (*decoded)[0]->definition);
  auto reencoded = EncodeSkillRecordV1(*(*decoded)[0], kKind);
  ASSERT_TRUE(reencoded.has_value());
  EXPECT_EQ(records.front().plaintext, *reencoded);
  EXPECT_EQ(crypto::hash::Sha256(original),
            crypto::hash::Sha256((*decoded)[0]->definition));
}

TEST(BackupSavedFlowRecordCodecTest,
     CommittedLifecycleRoundTripsBesideBothVersionTwoDefinitions) {
  for (bool associated : {false, true}) {
    auto skill = RecordedSkill(associated);
    for (auto status : {mojom::SkillStatus::kDraft, mojom::SkillStatus::kActive,
                        mojom::SkillStatus::kDisabled,
                        mojom::SkillStatus::kSuperseded,
                        mojom::SkillStatus::kRetired}) {
      SCOPED_TRACE(static_cast<int>(status));
      SCOPED_TRACE(associated);
      skill->status = status;
      ASSERT_NO_FATAL_FAILURE(ExpectExactRoundTrip(*skill));
    }
  }
}

TEST(BackupSavedFlowRecordCodecTest,
     HistoricalVersionOneDraftKeepsItsCommittedLifecycle) {
  auto skill = test::CurrentProcedure(kSkillId, 1u, kKind);
  for (auto status : {mojom::SkillStatus::kActive, mojom::SkillStatus::kDisabled,
                      mojom::SkillStatus::kSuperseded}) {
    skill->status = status;
    ASSERT_NO_FATAL_FAILURE(ExpectExactRoundTrip(*skill));
  }
}

TEST(BackupSavedFlowRecordCodecTest,
     UnknownFormatAndEitherUnknownStatusAreRefused) {
  for (uint8_t format : {0u, 3u, 255u}) {
    auto skill = RecordedSkill();
    skill->definition[5] = format;
    EXPECT_FALSE(EncodeSkillRecordV1(*skill, kKind).has_value());
  }
  auto skill = RecordedSkill();
  skill->status = static_cast<mojom::SkillStatus>(255u);
  EXPECT_FALSE(EncodeSkillRecordV1(*skill, kKind).has_value());
  skill = RecordedSkill();
  // The frozen draft label starts at offset 32; changing it to "xraft"
  // cannot be hidden by the valid Active lifecycle in the row.
  ASSERT_EQ('d', skill->definition[32]);
  skill->definition[32] = 'x';
  EXPECT_FALSE(EncodeSkillRecordV1(*skill, kKind).has_value());
}

TEST(BackupSavedFlowRecordCodecTest,
     MalformedVersionTwoTaskAssociationNeverReachesTheOrigin) {
  constexpr size_t kAssociation = 57u;
  constexpr size_t kOrigin = 76u;
  std::vector<std::vector<uint8_t>> malformed = {
      {}, {2u}, {1u}, {1u, 0u}, {1u, 0u, 0u},
      {1u, 0u, 1u, '/'}, {1u, 0u, 1u, 0xffu}};
  auto& oversized = malformed.emplace_back(std::vector<uint8_t>{1u, 1u, 1u});
  oversized.insert(oversized.end(), 257u, 't');
  for (const auto& association : malformed) {
    auto skill = RecordedSkill();
    ASSERT_EQ(1u, skill->definition[kAssociation]);
    skill->definition.erase(skill->definition.begin() + kAssociation,
                            skill->definition.begin() + kOrigin);
    skill->definition.insert(skill->definition.begin() + kAssociation,
                             association.begin(), association.end());
    EXPECT_FALSE(EncodeSkillRecordV1(*skill, kKind).has_value());
  }
}

TEST(BackupSavedFlowRecordCodecTest,
     ImmutableIdentityAndDescriptorBindingStillRefuseSubstitution) {
  std::vector<mojom::SkillRecordPtr> variants;
  for (size_t index = 0u; index < 5u; ++index) {
    variants.push_back(RecordedSkill());
  }
  variants[0]->skill_id = "other-skill";
  variants[1]->origin = "https://other.test";
  variants[2]->active_version = 2u;
  variants[3]->provenance = mojom::SkillProvenance::kAuthored;
  variants[4]->step_count = 2u;
  for (const auto& skill : variants) {
    EXPECT_FALSE(EncodeSkillRecordV1(*skill, kKind).has_value());
  }
  auto encoded = EncodeSkillRecordV1(*RecordedSkill(), kKind);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_FALSE(DecodeSkillRecordV1(*encoded, kKind, "other-skill", 1u)
                   .has_value());
  EXPECT_FALSE(DecodeSkillRecordV1(*encoded, kKind, kSkillId, 2u).has_value());
  EXPECT_FALSE(DecodeSkillRecordV1(
                   *encoded, mojom::BackupRecordKind::kUserAuthoredSkill,
                   kSkillId, 1u)
                   .has_value());
}

TEST(BackupSavedFlowRecordCodecTest,
     PayloadDigestStillBindsLifecycleAndEveryDefinitionByte) {
  for (bool change_lifecycle : {false, true}) {
    auto skill = RecordedSkill();
    std::vector<BackupSnapshotRecord> records;
    records.push_back(Snapshot(*skill));
    ASSERT_TRUE(records.front().descriptor);
    if (change_lifecycle) {
      skill->status = mojom::SkillStatus::kDisabled;
    } else {
      // Another well-formed public address, so native header validation alone
      // cannot reject it. Its bytes no longer match the retained archive.
      skill->definition.back() = 'x';
    }
    auto changed = EncodeSkillRecordV1(*skill, kKind);
    ASSERT_TRUE(changed.has_value());
    ASSERT_TRUE(
        DecodeSkillRecordV1(*changed, kKind, kSkillId, 1u).has_value());
    records.front().plaintext = std::move(*changed);
    auto refused = DecodeSelectedBackupSkillRecords(records);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(BackupSkillProjectionError::kInvalidRecord, refused.error());
  }
}

}  // namespace
}  // namespace taffy::storage::backup
