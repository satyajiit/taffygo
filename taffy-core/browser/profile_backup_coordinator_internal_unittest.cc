// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_coordinator_internal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "crypto/hash.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/components/storage/browser/encrypted_backup_archive.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

constexpr storage::backup::Secret kRecoveryKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
constexpr std::array<uint8_t, 3> kLibraryPayload = {3, 1, 4};
constexpr std::array<uint8_t, 2> kMemoryPayload = {1, 5};

storage::backup::BackupSnapshotRecord ActiveRecord(
    core_mojom::BackupRecordKind kind,
    std::string stable_id,
    base::span<const uint8_t> plaintext) {
  storage::backup::BackupSnapshotRecord record;
  record.plaintext.assign(plaintext.begin(), plaintext.end());
  record.descriptor = core_mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = std::move(stable_id);
  record.descriptor->revision = 1u;
  record.descriptor->schema_version = 1u;
  record.descriptor->state = core_mojom::BackupRecordState::kActive;
  record.descriptor->plaintext_bytes = record.plaintext.size();
  const auto digest = crypto::hash::Sha256(record.plaintext);
  record.descriptor->plaintext_sha256.assign(digest.begin(), digest.end());
  return record;
}

storage::backup::BackupSnapshotRecord TombstoneRecord(
    core_mojom::BackupRecordKind kind,
    std::string stable_id) {
  storage::backup::BackupSnapshotRecord record;
  record.descriptor = core_mojom::BackupRecordDescriptor::New();
  record.descriptor->kind = kind;
  record.descriptor->stable_id = std::move(stable_id);
  record.descriptor->revision = 2u;
  record.descriptor->schema_version = 1u;
  record.descriptor->state = core_mojom::BackupRecordState::kTombstone;
  record.descriptor->plaintext_sha256.assign(32u, 0u);
  return record;
}

storage::backup::BackupSnapshotResult Snapshot(
    std::vector<storage::backup::BackupSnapshotRecord> records) {
  return storage::backup::BackupSnapshotResult(std::move(records));
}

base::File FileWithBytes(const base::FilePath& directory,
                         base::span<const uint8_t> bytes) {
  base::FilePath path;
  base::File file = base::CreateAndOpenTemporaryFileInDir(directory, &path);
  if (!file.IsValid() || !file.WriteAtCurrentPosAndCheck(bytes) ||
      !file.Flush()) {
    return base::File();
  }
  return file;
}

core_mojom::BackupManifestInspectResultPtr Inspection(
    std::vector<core_mojom::BackupPayloadLayoutEntryPtr> records,
    uint64_t payload_bytes) {
  auto result = core_mojom::BackupManifestInspectResult::New();
  result->status = core_mojom::BackupPlanningStatus::kSucceeded;
  result->selection = {core_mojom::BackupRecordKind::kLibraryEntry};
  result->record_count = records.size();
  result->payload_plaintext_bytes = payload_bytes;
  result->records = std::move(records);
  return result;
}

TEST(ProfileBackupCoordinatorInternalTest,
     SnapshotRefusesDigestDriftAndDuplicateIdentity) {
  const std::array selection = {core_mojom::BackupRecordKind::kLibraryEntry};
  auto drift = ActiveRecord(core_mojom::BackupRecordKind::kLibraryEntry,
                            std::string(32u, 'a'), kLibraryPayload);
  drift.plaintext[0] ^= 0xffu;
  std::vector<storage::backup::BackupSnapshotRecord> drift_records;
  drift_records.push_back(std::move(drift));
  auto drift_result =
      ValidateBackupSnapshot(Snapshot(std::move(drift_records)), selection);
  ASSERT_FALSE(drift_result);
  EXPECT_EQ(ProfileBackupError::kSnapshotMismatch, drift_result.error());

  std::vector<storage::backup::BackupSnapshotRecord> duplicate;
  duplicate.push_back(ActiveRecord(core_mojom::BackupRecordKind::kLibraryEntry,
                                   std::string(32u, 'b'), kLibraryPayload));
  duplicate.push_back(TombstoneRecord(
      core_mojom::BackupRecordKind::kLibraryEntry, std::string(32u, 'b')));
  auto duplicate_result =
      ValidateBackupSnapshot(Snapshot(std::move(duplicate)), selection);
  ASSERT_FALSE(duplicate_result);
  EXPECT_EQ(ProfileBackupError::kSnapshotMismatch, duplicate_result.error());
}

TEST(ProfileBackupCoordinatorInternalTest,
     UnsupportedSelectionRefusesEvenAnEmptySnapshot) {
  const std::array unsupported = {core_mojom::BackupRecordKind::kBookmark};
  auto result = ValidateBackupSnapshot(Snapshot({}), unsupported);
  ASSERT_FALSE(result);
  EXPECT_EQ(ProfileBackupError::kInvalidArgument, result.error());
}

TEST(ProfileBackupCoordinatorInternalTest,
     CoreSourceOrderIsTheOnlyPayloadOrder) {
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  auto stage = storage::backup::BackupArchiveStageStore::Create(
      profile.GetPath().Append(storage::backup::kBackupStagingDirectoryName));
  ASSERT_TRUE(stage);
  const std::array selection = {
      core_mojom::BackupRecordKind::kLibraryEntry,
      core_mojom::BackupRecordKind::kMemoryRecord,
  };
  std::vector<storage::backup::BackupSnapshotRecord> records;
  records.push_back(ActiveRecord(core_mojom::BackupRecordKind::kLibraryEntry,
                                 std::string(32u, 'c'), kLibraryPayload));
  records.push_back(ActiveRecord(core_mojom::BackupRecordKind::kMemoryRecord,
                                 std::string(32u, 'd'), kMemoryPayload));
  auto validated =
      ValidateBackupSnapshot(Snapshot(std::move(records)), selection);
  ASSERT_TRUE(validated);
  std::array<uint8_t, 32> snapshot_digest{};
  snapshot_digest[0] = 1u;
  auto sealed = SealValidatedBackupSnapshot(
      stage, "export-order", SensitiveBackupSecret(kRecoveryKey), {7u, 8u},
      snapshot_digest, {1u, 0u}, 5u, std::move(*validated));
  ASSERT_TRUE(sealed);

  base::File readback =
      stage->OpenExportReadback("export-order", sealed->archive_bytes);
  ASSERT_TRUE(readback.IsValid());
  base::File encrypted = stage->OpenEncryptedExport("export-order");
  ASSERT_TRUE(encrypted.IsValid());
  base::File plaintext = FileWithBytes(profile.GetPath(), {});
  ASSERT_TRUE(plaintext.IsValid());
  auto opened =
      storage::backup::OpenAibArchive(&encrypted, &plaintext, kRecoveryKey);
  ASSERT_TRUE(opened);
  ASSERT_EQ(5, plaintext.GetLength());
  std::array<uint8_t, 5> actual{};
  ASSERT_TRUE(plaintext.ReadAndCheck(0, actual));
  EXPECT_EQ((std::array<uint8_t, 5>{1, 5, 3, 1, 4}), actual);
}

TEST(ProfileBackupCoordinatorInternalTest,
     DuplicateSourceOrdinalCannotCreateAStage) {
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  auto stage = storage::backup::BackupArchiveStageStore::Create(
      profile.GetPath().Append(storage::backup::kBackupStagingDirectoryName));
  ASSERT_TRUE(stage);
  const std::array selection = {core_mojom::BackupRecordKind::kLibraryEntry};
  std::vector<storage::backup::BackupSnapshotRecord> records;
  records.push_back(ActiveRecord(core_mojom::BackupRecordKind::kLibraryEntry,
                                 std::string(32u, 'e'), kLibraryPayload));
  auto validated =
      ValidateBackupSnapshot(Snapshot(std::move(records)), selection);
  ASSERT_TRUE(validated);
  std::array<uint8_t, 32> snapshot_digest{};
  snapshot_digest[0] = 1u;
  auto sealed = SealValidatedBackupSnapshot(
      stage, "bad-order", SensitiveBackupSecret(kRecoveryKey), {1u},
      snapshot_digest, {1u}, kLibraryPayload.size(), std::move(*validated));
  ASSERT_FALSE(sealed);
  EXPECT_EQ(ProfileBackupError::kPlanRefused, sealed.error());
  EXPECT_EQ(0u, stage->size_for_testing());
}

TEST(ProfileBackupCoordinatorInternalTest,
     ImportHashesExactActiveRangesAndTombstoneSentinel) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  VerifiedBackupImportPayload imported;
  imported.verified.payload_plaintext_bytes = kLibraryPayload.size();
  imported.verified.plaintext_payload =
      FileWithBytes(directory.GetPath(), kLibraryPayload);
  std::vector<core_mojom::BackupPayloadLayoutEntryPtr> layout;
  layout.push_back(core_mojom::BackupPayloadLayoutEntry::New(
      core_mojom::BackupRecordState::kActive, kLibraryPayload.size()));
  layout.push_back(core_mojom::BackupPayloadLayoutEntry::New(
      core_mojom::BackupRecordState::kTombstone, 0u));
  auto inspection = Inspection(std::move(layout), 3u);
  auto result = HashBackupImportPayload(std::move(imported), *inspection);
  ASSERT_TRUE(result);
  ASSERT_EQ(2u, result->staged_records.size());
  const auto digest = crypto::hash::Sha256(kLibraryPayload);
  EXPECT_TRUE(
      std::ranges::equal(digest, result->staged_records[0]->plaintext_sha256));
  EXPECT_TRUE(std::ranges::all_of(result->staged_records[1]->plaintext_sha256,
                                  [](uint8_t byte) { return byte == 0u; }));
}

TEST(ProfileBackupCoordinatorInternalTest,
     ImportRefusesLengthDriftAndUnsupportedEmptySelection) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  VerifiedBackupImportPayload trailing;
  trailing.verified.payload_plaintext_bytes = 3u;
  const std::array<uint8_t, 4> changed = {3, 1, 4, 1};
  trailing.verified.plaintext_payload =
      FileWithBytes(directory.GetPath(), changed);
  std::vector<core_mojom::BackupPayloadLayoutEntryPtr> layout;
  layout.push_back(core_mojom::BackupPayloadLayoutEntry::New(
      core_mojom::BackupRecordState::kActive, 3u));
  auto drift_inspection = Inspection(std::move(layout), 3u);
  auto drift = HashBackupImportPayload(std::move(trailing), *drift_inspection);
  ASSERT_FALSE(drift);
  EXPECT_EQ(ProfileBackupError::kSnapshotMismatch, drift.error());

  VerifiedBackupImportPayload empty;
  empty.verified.plaintext_payload = FileWithBytes(directory.GetPath(), {});
  auto unsupported = Inspection({}, 0u);
  unsupported->selection = {core_mojom::BackupRecordKind::kBrowserPreference};
  auto empty_result = HashBackupImportPayload(std::move(empty), *unsupported);
  ASSERT_FALSE(empty_result);
  EXPECT_EQ(ProfileBackupError::kSnapshotMismatch, empty_result.error());
}

TEST(ProfileBackupCoordinatorInternalTest,
     FreshDecisionUsesDistinctBoundedIdentitiesForEveryAttempt) {
  const std::string public_id(core_mojom::kMaxOperationIdBytes, 'a');
  auto first = NewBackupOperation(7u, "backup-restore-confirm", public_id);
  auto retry = NewBackupOperation(7u, "backup-restore-confirm", public_id);
  auto next = NewBackupOperation(8u, "backup-restore-confirm", public_id);
  ASSERT_TRUE(first);
  ASSERT_TRUE(retry);
  ASSERT_TRUE(next);
  EXPECT_NE(first->operation_id, retry->operation_id);
  EXPECT_NE(first->idempotency_key, retry->idempotency_key);
  EXPECT_NE(first->operation_id, next->operation_id);
  EXPECT_NE(first->idempotency_key, next->idempotency_key);
  const uint64_t now = BackupPlanningNowMonotonicMillis();
  EXPECT_TRUE(IsLiveBackupOperation(first.get(), 7u, now));
  EXPECT_TRUE(IsLiveBackupOperation(retry.get(), 7u, now));
  EXPECT_TRUE(IsLiveBackupOperation(next.get(), 8u, now));
  EXPECT_FALSE(IsLiveBackupOperation(first.get(), 8u, now));
}

TEST(ProfileBackupCoordinatorInternalTest,
     DecisionFactoryRefusesMalformedOrUnboundedIdentity) {
  EXPECT_FALSE(NewBackupOperation(0u, "backup-restore-cancel", "import"));
  EXPECT_FALSE(NewBackupOperation(7u, "", "import"));
  EXPECT_FALSE(NewBackupOperation(7u, "backup-restore-cancel", ""));
  EXPECT_FALSE(NewBackupOperation(7u, "bad\nkind", "import"));
  EXPECT_FALSE(NewBackupOperation(7u, "backup-restore-cancel",
                                  std::string(1u, static_cast<char>(0xff))));
  EXPECT_FALSE(NewBackupOperation(
      7u, std::string(core_mojom::kMaxOperationIdBytes, 'k'), "import"));
}

}  // namespace
}  // namespace taffy
