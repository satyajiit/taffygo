// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_projection.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup::restore_target_internal {
namespace {

namespace mojom = core_service::mojom;

BackupSnapshotRecord Active(mojom::BackupRecordKind kind,
                            std::string stable_id,
                            uint64_t revision,
                            std::string plaintext) {
  BackupSnapshotRecord record;
  record.plaintext.assign(plaintext.begin(), plaintext.end());
  record.descriptor = mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = std::move(stable_id);
  record.descriptor->revision = revision;
  record.descriptor->schema_version = 1u;
  record.descriptor->state = mojom::BackupRecordState::kActive;
  record.descriptor->plaintext_bytes = record.plaintext.size();
  const auto digest = crypto::hash::Sha256(record.plaintext);
  record.descriptor->plaintext_sha256.assign(digest.begin(), digest.end());
  return record;
}

BackupSnapshotRecord Tombstone(mojom::BackupRecordKind kind,
                               std::string stable_id,
                               uint64_t revision) {
  BackupSnapshotRecord record;
  record.descriptor = mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = std::move(stable_id);
  record.descriptor->revision = revision;
  record.descriptor->schema_version = 1u;
  record.descriptor->state = mojom::BackupRecordState::kTombstone;
  record.descriptor->plaintext_sha256.assign(32u, 0u);
  return record;
}

std::vector<BackupSnapshotRecord> MixedRecords(bool scrambled) {
  std::vector<BackupSnapshotRecord> records;
  if (scrambled) {
    records.push_back(Tombstone(mojom::BackupRecordKind::kMemoryRecord,
                                "22222222222222222222222222222222", 5u));
    records.push_back(Active(mojom::BackupRecordKind::kLibraryEntry,
                             "11111111111111111111111111111111", 8u,
                             "library-v1"));
    records.push_back(Active(mojom::BackupRecordKind::kAssistantConfiguration,
                             kAssistantConfigurationBackupStableId, 7u,
                             "config-v1"));
  } else {
    records.push_back(Active(mojom::BackupRecordKind::kAssistantConfiguration,
                             kAssistantConfigurationBackupStableId, 7u,
                             "config-v1"));
    records.push_back(Active(mojom::BackupRecordKind::kLibraryEntry,
                             "11111111111111111111111111111111", 8u,
                             "library-v1"));
    records.push_back(Tombstone(mojom::BackupRecordKind::kMemoryRecord,
                                "22222222222222222222222222222222", 5u));
  }
  return records;
}

std::vector<BackupSnapshotRecord> SixKindRecords(bool scrambled) {
  std::vector<BackupSnapshotRecord> records;
  records.push_back(Active(mojom::BackupRecordKind::kAssistantConfiguration,
                           kAssistantConfigurationBackupStableId, 7u,
                           "config-v1"));
  records.push_back(Active(mojom::BackupRecordKind::kSavedWorkspace,
                           "33333333333333333333333333333333", 9u,
                           "workspace-v1"));
  records.push_back(Active(mojom::BackupRecordKind::kLibraryEntry,
                           "11111111111111111111111111111111", 8u,
                           "library-v1"));
  records.push_back(Tombstone(mojom::BackupRecordKind::kMemoryRecord,
                              "22222222222222222222222222222222", 5u));
  records.push_back(Active(mojom::BackupRecordKind::kUserAuthoredSkill,
                           "a-skill", 2u, "authored-v1"));
  records.push_back(Active(mojom::BackupRecordKind::kLearnedProcedure,
                           "z-procedure", 3u, "learned-v1"));
  if (scrambled) {
    std::ranges::reverse(records);
  }
  return records;
}

TEST(DormantBackupRestoreTargetProjectionTest,
     EmptyProjectionHasFrozenNonzeroVersionOneWitness) {
  auto witness = BuildCandidateWitness({});

  ASSERT_TRUE(witness);
  EXPECT_TRUE(witness->selection.empty());
  EXPECT_EQ(witness->record_count, 0u);
  EXPECT_EQ(base::HexEncodeLower(witness->candidate_records_sha256),
            "ccb7752a02f27b581e0a44c3ae78067906e83e0b4c6a23af5d8bf35e2a350da1");
  EXPECT_TRUE(IsValidCandidateWitness(*witness));
}

TEST(DormantBackupRestoreTargetProjectionTest,
     MixedActiveAndTombstoneWitnessIsFrozenAndOrderIndependent) {
  auto canonical = BuildCandidateWitness(MixedRecords(false));
  auto scrambled = BuildCandidateWitness(MixedRecords(true));

  ASSERT_TRUE(canonical);
  ASSERT_TRUE(scrambled);
  ASSERT_EQ(canonical->selection.size(), 3u);
  EXPECT_EQ(canonical->selection[0],
            mojom::BackupRecordKind::kAssistantConfiguration);
  EXPECT_EQ(canonical->selection[1], mojom::BackupRecordKind::kLibraryEntry);
  EXPECT_EQ(canonical->selection[2], mojom::BackupRecordKind::kMemoryRecord);
  EXPECT_EQ(canonical->record_count, 3u);
  EXPECT_EQ(base::HexEncodeLower(canonical->candidate_records_sha256),
            "7a1e0a2df0dc8df924eaf4964561d0ad697ffabdf130ecf7ad1bd8ae63ebbb9c");
  EXPECT_TRUE(IsExactCandidateWitness(*canonical, *scrambled));
}

TEST(DormantBackupRestoreTargetProjectionTest,
     SixKindWitnessTagsAreFrozenAndOrderIndependent) {
  auto canonical = BuildCandidateWitness(SixKindRecords(false));
  auto scrambled = BuildCandidateWitness(SixKindRecords(true));

  ASSERT_TRUE(canonical);
  ASSERT_TRUE(scrambled);
  ASSERT_EQ(canonical->selection.size(), 6u);
  EXPECT_EQ(canonical->selection[0],
            mojom::BackupRecordKind::kAssistantConfiguration);
  EXPECT_EQ(canonical->selection[1], mojom::BackupRecordKind::kSavedWorkspace);
  EXPECT_EQ(canonical->selection[2], mojom::BackupRecordKind::kLibraryEntry);
  EXPECT_EQ(canonical->selection[3], mojom::BackupRecordKind::kMemoryRecord);
  EXPECT_EQ(canonical->selection[4],
            mojom::BackupRecordKind::kUserAuthoredSkill);
  EXPECT_EQ(canonical->selection[5],
            mojom::BackupRecordKind::kLearnedProcedure);
  EXPECT_EQ(canonical->record_count, 6u);
  EXPECT_EQ(base::HexEncodeLower(canonical->candidate_records_sha256),
            "c0c7a9364a692d5c82197a7c39e2dfac39d09df6def343e5a17348105b6567a7");
  EXPECT_TRUE(IsExactCandidateWitness(*canonical, *scrambled));
}

TEST(DormantBackupRestoreTargetProjectionTest,
     WitnessValidationClosesCardinalityKindOrderAndDigest) {
  auto valid = BuildCandidateWitness(MixedRecords(false));
  ASSERT_TRUE(valid);

  auto too_many_kinds = valid.Clone();
  too_many_kinds->record_count = 2u;
  EXPECT_FALSE(IsValidCandidateWitness(*too_many_kinds));

  auto unordered = valid.Clone();
  std::swap(unordered->selection[0], unordered->selection[1]);
  EXPECT_FALSE(IsValidCandidateWitness(*unordered));

  auto duplicate = valid.Clone();
  duplicate->selection[1] = duplicate->selection[0];
  EXPECT_FALSE(IsValidCandidateWitness(*duplicate));

  auto zero_digest = valid.Clone();
  zero_digest->candidate_records_sha256.assign(32u, 0u);
  EXPECT_FALSE(IsValidCandidateWitness(*zero_digest));

  auto empty_with_count = BuildCandidateWitness({});
  ASSERT_TRUE(empty_with_count);
  empty_with_count->record_count = 1u;
  EXPECT_FALSE(IsValidCandidateWitness(*empty_with_count));
}

TEST(DormantBackupRestoreTargetProjectionTest,
     ImportedMetadataRoleNumbersAreVersionedFormat) {
  static_assert(static_cast<uint32_t>(ImportedMetadataRole::kRecord) == 0u);
  static_assert(
      static_cast<uint32_t>(ImportedMetadataRole::kLibraryCollection) == 1u);
  static_assert(
      static_cast<uint32_t>(ImportedMetadataRole::kMemoryCollection) == 2u);

  auto witness = BuildCandidateWitness(MixedRecords(false));
  ASSERT_TRUE(witness);
  constexpr std::string_view kTarget = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
  EXPECT_EQ(ImportedMetadataId(kTarget, *witness, ImportedMetadataRole::kRecord,
                               mojom::BackupRecordKind::kLibraryEntry,
                               "11111111111111111111111111111111"),
            "backup-restore-v1-"
            "c8404f718a4da21f00f2449caac8fe860bb85ac898bc2560a845fa8cb2da2aa2");
  EXPECT_EQ(ImportedMetadataId(kTarget, *witness,
                               ImportedMetadataRole::kLibraryCollection,
                               mojom::BackupRecordKind::kLibraryEntry, {}),
            "backup-restore-v1-"
            "bc24db37f116bbfec42999a68db92f16448091c29a45f1e1eb9c8b40e0f14c96");
  EXPECT_EQ(ImportedMetadataId(kTarget, *witness,
                               ImportedMetadataRole::kMemoryCollection,
                               mojom::BackupRecordKind::kMemoryRecord, {}),
            "backup-restore-v1-"
            "3ac84929b5401bf2c8306df6119ad4a345326e774e221b730cdcceb58bf571ea");
}

}  // namespace
}  // namespace taffy::storage::backup::restore_target_internal
