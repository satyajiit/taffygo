// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "crypto/hash.h"
#include "taffy/browser/profile_backup_coordinator_internal.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Kind = mojom::BackupRecordKind;
using Record = storage::backup::BackupSnapshotRecord;
constexpr std::array kSqlKinds = {
    Kind::kAssistantConfiguration, Kind::kSavedWorkspace,
    Kind::kLibraryEntry,           Kind::kMemoryRecord,
    Kind::kUserAuthoredSkill,      Kind::kLearnedProcedure};

// This suite exercises the descriptor/digest boundary, not the payload codecs.
// The typed codecs have their own round-trip and malformed-payload suites.
Record Descriptor(Kind kind, std::string id) {
  Record record;
  record.plaintext = {1, 2, 3};
  record.descriptor = mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = std::move(id);
  record.descriptor->revision = 1;
  record.descriptor->schema_version = 1;
  record.descriptor->state = mojom::BackupRecordState::kActive;
  record.descriptor->plaintext_bytes = record.plaintext.size();
  const auto digest = crypto::hash::Sha256(record.plaintext);
  record.descriptor->plaintext_sha256.assign(digest.begin(), digest.end());
  return record;
}

auto ValidateOne(Record record) {
  const std::array selection{record.descriptor->kind};
  std::vector<Record> records;
  records.push_back(std::move(record));
  return ValidateBackupSnapshot(std::move(records), selection);
}

TEST(ProfileBackupCoordinatorSelectionTest,
     AllSixStorageKindsReachManifestPreparation) {
  ASSERT_TRUE(storage::backup::IsSupportedBackupStorageSelection(kSqlKinds));
  ASSERT_TRUE(IsSupportedBackupManifestSelection(kSqlKinds));
  std::vector<Record> records;
  for (auto kind : kSqlKinds) {
    std::string id(32, 'a');
    if (kind == Kind::kAssistantConfiguration) {
      id = storage::backup::kAssistantConfigurationBackupStableId;
    } else if (kind == Kind::kUserAuthoredSkill) {
      id = "authored-procedure";
    } else if (kind == Kind::kLearnedProcedure) {
      id = "learned-procedure";
    }
    records.push_back(Descriptor(kind, std::move(id)));
  }
  auto result = ValidateBackupSnapshot(std::move(records), kSqlKinds);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->records.size(), 6u);
  EXPECT_EQ(result->descriptors.size(), 6u);
  EXPECT_EQ(result->payload_plaintext_bytes, 18u);
}

TEST(ProfileBackupCoordinatorSelectionTest,
     OnlyKindsWithStoredDeletionMarkersAdmitTombstones) {
  for (auto kind : kSqlKinds) {
    auto record = Descriptor(kind, std::string(32, 'a'));
    if (kind == Kind::kAssistantConfiguration) {
      record.descriptor->stable_id =
          storage::backup::kAssistantConfigurationBackupStableId;
    }
    record.plaintext.clear();
    record.descriptor->plaintext_bytes = 0;
    record.descriptor->plaintext_sha256.assign(32, 0);
    record.descriptor->state = mojom::BackupRecordState::kTombstone;
    auto result = ValidateOne(std::move(record));
    EXPECT_EQ(result.has_value(), kind == Kind::kSavedWorkspace ||
                                      kind == Kind::kLibraryEntry ||
                                      kind == Kind::kMemoryRecord);
  }
}

TEST(ProfileBackupCoordinatorSelectionTest,
     ExtendedKindsRetainDescriptorBounds) {
  EXPECT_FALSE(
      ValidateOne(Descriptor(Kind::kSavedWorkspace, "not-a-workspace-id")));
  for (const auto* id : {"", "Uppercase", "has space", "has/slash"}) {
    EXPECT_FALSE(ValidateOne(Descriptor(Kind::kUserAuthoredSkill, id)));
    EXPECT_FALSE(ValidateOne(Descriptor(Kind::kLearnedProcedure, id)));
  }
  auto too_long = Descriptor(Kind::kLearnedProcedure,
                             std::string(mojom::kMaxSkillIdBytes + 1, 'a'));
  EXPECT_FALSE(ValidateOne(std::move(too_long)));
  auto too_new = Descriptor(Kind::kUserAuthoredSkill, "valid-id");
  too_new.descriptor->revision = mojom::kMaxSkillVersionsPerSkill + 1;
  EXPECT_FALSE(ValidateOne(std::move(too_new)));
  auto overflow = Descriptor(Kind::kSavedWorkspace, std::string(32, 'a'));
  overflow.descriptor->revision = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(ValidateOne(std::move(overflow)));
  auto wrong_size = Descriptor(Kind::kSavedWorkspace, std::string(32, 'a'));
  wrong_size.descriptor->plaintext_bytes = wrong_size.plaintext.size() - 1;
  EXPECT_FALSE(ValidateOne(std::move(wrong_size)));
}

TEST(ProfileBackupCoordinatorSelectionTest,
     SharedProcedureIdentityCannotHideBehindDifferentKinds) {
  std::vector<Record> records;
  records.push_back(Descriptor(Kind::kUserAuthoredSkill, "same-procedure"));
  records.push_back(Descriptor(Kind::kLearnedProcedure, "same-procedure"));
  auto result = ValidateBackupSnapshot(
      std::move(records),
      std::array{Kind::kUserAuthoredSkill, Kind::kLearnedProcedure});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ProfileBackupError::kSnapshotMismatch);
}

TEST(ProfileBackupCoordinatorSelectionTest,
     CombinedProcedureCountRemainsBounded) {
  std::vector<Record> records;
  for (size_t index = 0; index <= mojom::kMaxSkillsPerProfile; ++index) {
    records.push_back(Descriptor(
        index % 2 == 0 ? Kind::kUserAuthoredSkill : Kind::kLearnedProcedure,
        "procedure-" + std::to_string(index)));
  }
  EXPECT_FALSE(ValidateBackupSnapshot(
      std::move(records),
      std::array{Kind::kUserAuthoredSkill, Kind::kLearnedProcedure}));
}

TEST(ProfileBackupCoordinatorSelectionTest,
     UnknownRecordStateIsNotAnImplicitActiveRecord) {
  auto record = Descriptor(Kind::kSavedWorkspace, std::string(32, 'a'));
  record.descriptor->state = static_cast<mojom::BackupRecordState>(99);
  EXPECT_FALSE(ValidateOne(std::move(record)));
}

TEST(ProfileBackupCoordinatorSelectionTest,
     BrowserOwnedClassesRemainDisabledUntilRestoreIsWired) {
  for (auto kind : {Kind::kBookmark, Kind::kBrowserPreference}) {
    const std::array selection{kind};
    EXPECT_FALSE(IsSupportedBackupManifestSelection(selection));
    EXPECT_FALSE(ValidateBackupSnapshot(std::vector<Record>{}, selection));
  }
}

TEST(ProfileBackupCoordinatorSelectionTest,
     UnsupportedSourceSelectionIsInvalidArgument) {
  auto result = ValidateBackupSnapshot(
      base::unexpected(
          storage::backup::BackupSnapshotError::kUnsupportedSelection),
      std::array{Kind::kLibraryEntry});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ProfileBackupError::kInvalidArgument);
}

TEST(ProfileBackupCoordinatorSelectionTest,
     UnavailableSourceIsStorageUnavailable) {
  auto result = ValidateBackupSnapshot(
      base::unexpected(storage::backup::BackupSnapshotError::kUnavailable),
      std::array{Kind::kLibraryEntry});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ProfileBackupError::kStorageUnavailable);
}

TEST(ProfileBackupCoordinatorSelectionTest,
     InvalidSourceRecordIsSnapshotMismatch) {
  auto result = ValidateBackupSnapshot(
      base::unexpected(storage::backup::BackupSnapshotError::kInvalidRecord),
      std::array{Kind::kLibraryEntry});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ProfileBackupError::kSnapshotMismatch);
}

TEST(ProfileBackupCoordinatorSelectionTest, SourceCapacityIsRefusalNotOutage) {
  auto result = ValidateBackupSnapshot(
      base::unexpected(storage::backup::BackupSnapshotError::kCapacityExceeded),
      std::array{Kind::kLibraryEntry});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ProfileBackupError::kPlanRefused);
}

}  // namespace
}  // namespace taffy
