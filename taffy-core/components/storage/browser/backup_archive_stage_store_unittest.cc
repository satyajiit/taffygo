// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_archive_stage_store.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

constexpr Secret kRecoveryKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
constexpr std::array<uint8_t, 6> kManifest = {1, 3, 3, 7, 4, 2};
constexpr std::array<uint8_t, 9> kPayload = {8, 6, 7, 5, 3, 0, 9, 1, 2};

std::vector<uint8_t> ReadAll(base::File* file) {
  const int64_t length = file->GetLength();
  if (length < 0 || file->Seek(base::File::FROM_BEGIN, 0) != 0) {
    return {};
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(length));
  if (!file->ReadAtCurrentPosAndCheck(bytes)) {
    return {};
  }
  return bytes;
}

bool WriteAll(base::File* file, base::span<const uint8_t> bytes) {
  return file->Seek(base::File::FROM_BEGIN, 0) == 0 &&
         file->WriteAtCurrentPosAndCheck(bytes) && file->Flush();
}

class BackupArchiveStageStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(profile_.CreateUniqueTempDir());
    staging_directory_ = profile_.GetPath().Append(kBackupStagingDirectoryName);
    store_ = BackupArchiveStageStore::Create(staging_directory_);
    ASSERT_TRUE(store_);
    ASSERT_TRUE(base::WriteFile(PayloadPath(), kPayload));
  }

  base::FilePath PayloadPath() const {
    return profile_.GetPath().AppendASCII("payload");
  }

  base::expected<PreparedBackupExport, BackupStageError> PrepareExport(
      std::string_view operation_id) {
    base::File payload(PayloadPath(),
                       base::File::FLAG_OPEN | base::File::FLAG_READ);
    return store_->PrepareExport(std::string(operation_id), std::move(payload),
                                 kPayload.size(), kRecoveryKey, kManifest);
  }

  std::vector<uint8_t> ReadPreparedArchive(std::string_view operation_id) {
    const auto prepared = PrepareExport(operation_id);
    EXPECT_TRUE(prepared);
    if (!prepared) {
      return {};
    }
    base::File readback = store_->OpenExportReadback(std::string(operation_id),
                                                     prepared->archive_bytes);
    EXPECT_TRUE(readback.IsValid());
    base::File source = store_->OpenEncryptedExport(std::string(operation_id));
    EXPECT_TRUE(source.IsValid());
    std::vector<uint8_t> bytes = ReadAll(&source);
    source.Close();
    readback.Close();
    store_->Abandon(std::string(operation_id));
    return bytes;
  }

  base::ScopedTempDir profile_;
  base::FilePath staging_directory_;
  scoped_refptr<BackupArchiveStageStore> store_;
};

TEST_F(BackupArchiveStageStoreTest, ExportRequiresExactFdOrderAndReadback) {
  const auto prepared = PrepareExport("export-1");
  ASSERT_TRUE(prepared);
  EXPECT_FALSE(store_->OpenEncryptedExport("export-1").IsValid());
  EXPECT_FALSE(
      store_->OpenExportReadback("export-1", prepared->archive_bytes + 1u)
          .IsValid());

  base::File readback =
      store_->OpenExportReadback("export-1", prepared->archive_bytes);
  ASSERT_TRUE(readback.IsValid());
  base::File source = store_->OpenEncryptedExport("export-1");
  ASSERT_TRUE(source.IsValid());
  const std::vector<uint8_t> archive = ReadAll(&source);
  ASSERT_EQ(archive.size(), prepared->archive_bytes);
  ASSERT_TRUE(WriteAll(&readback, archive));
  source.Close();
  readback.Close();

  EXPECT_EQ(store_->VerifyExportReadback("export-1"),
            BackupStageStatus::kVerified);
  EXPECT_EQ(store_->VerifyExportReadback("export-1"),
            BackupStageStatus::kUnavailable);
  store_->Abandon("export-1");
  store_->Abandon("export-1");
  EXPECT_EQ(store_->size_for_testing(), 0u);
}

TEST_F(BackupArchiveStageStoreTest, PlaintextScratchHasNoObservablePath) {
  base::File scratch = store_->CreatePlaintextScratchFile();
  ASSERT_TRUE(scratch.IsValid());
  EXPECT_TRUE(scratch.WriteAtCurrentPosAndCheck(kPayload));
  EXPECT_EQ(static_cast<int64_t>(kPayload.size()), scratch.GetLength());

  base::FileEnumerator files(staging_directory_, false,
                             base::FileEnumerator::FILES);
  EXPECT_TRUE(files.Next().empty());
}

TEST_F(BackupArchiveStageStoreTest, TruncatedExportReadbackIsRefused) {
  const auto prepared = PrepareExport("export-short");
  ASSERT_TRUE(prepared);
  base::File readback =
      store_->OpenExportReadback("export-short", prepared->archive_bytes);
  ASSERT_TRUE(readback.IsValid());
  base::File source = store_->OpenEncryptedExport("export-short");
  ASSERT_TRUE(source.IsValid());
  std::vector<uint8_t> archive = ReadAll(&source);
  ASSERT_GT(archive.size(), 1u);
  ASSERT_TRUE(
      WriteAll(&readback, base::span(archive).first(archive.size() - 1u)));
  source.Close();
  readback.Close();

  EXPECT_EQ(store_->VerifyExportReadback("export-short"),
            BackupStageStatus::kRefused);
}

TEST_F(BackupArchiveStageStoreTest,
       ImportAuthenticatesBeforeExposingManifestAndPayload) {
  const std::vector<uint8_t> archive = ReadPreparedArchive("source");
  ASSERT_FALSE(archive.empty());
  ASSERT_TRUE(store_->PrepareImport("import-1", kRecoveryKey));
  EXPECT_FALSE(
      store_
          ->OpenEncryptedImport(
              "import-1", BackupArchiveStageStore::MaximumArchiveBytes() - 1u)
          .IsValid());
  base::File destination = store_->OpenEncryptedImport(
      "import-1", BackupArchiveStageStore::MaximumArchiveBytes());
  ASSERT_TRUE(destination.IsValid());
  ASSERT_TRUE(WriteAll(&destination, archive));
  destination.Close();

  EXPECT_EQ(store_->InspectImportedArchive("import-1", archive.size()),
            BackupStageStatus::kVerified);
  std::optional<VerifiedBackupImport> verified =
      store_->ReadVerifiedImport("import-1");
  ASSERT_TRUE(verified);
  EXPECT_EQ(verified->manifest_plaintext,
            std::vector<uint8_t>(kManifest.begin(), kManifest.end()));
  EXPECT_EQ(verified->payload_plaintext_bytes, kPayload.size());
  EXPECT_EQ(ReadAll(&verified->plaintext_payload),
            std::vector<uint8_t>(kPayload.begin(), kPayload.end()));
}

TEST_F(BackupArchiveStageStoreTest, ImportRefusesWrongLengthAndKey) {
  const std::vector<uint8_t> archive = ReadPreparedArchive("source");
  ASSERT_FALSE(archive.empty());
  ASSERT_TRUE(store_->PrepareImport("wrong-length", kRecoveryKey));
  base::File destination = store_->OpenEncryptedImport(
      "wrong-length", BackupArchiveStageStore::MaximumArchiveBytes());
  ASSERT_TRUE(WriteAll(&destination, archive));
  destination.Close();
  EXPECT_EQ(store_->InspectImportedArchive("wrong-length", archive.size() - 1u),
            BackupStageStatus::kRefused);
  EXPECT_FALSE(store_->ReadVerifiedImport("wrong-length"));

  Secret wrong_key = kRecoveryKey;
  wrong_key.front() ^= 1u;
  ASSERT_TRUE(store_->PrepareImport("wrong-key", wrong_key));
  destination = store_->OpenEncryptedImport(
      "wrong-key", BackupArchiveStageStore::MaximumArchiveBytes());
  ASSERT_TRUE(WriteAll(&destination, archive));
  destination.Close();
  EXPECT_EQ(store_->InspectImportedArchive("wrong-key", archive.size()),
            BackupStageStatus::kRefused);
  EXPECT_FALSE(store_->ReadVerifiedImport("wrong-key"));
}

TEST(BackupArchiveStageStoreInitializationTest,
     ClearsOnlyTheDedicatedProfileChildAndRemovesItOnDestruction) {
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  const base::FilePath staging =
      profile.GetPath().Append(kBackupStagingDirectoryName);
  const base::FilePath stale = staging.AppendASCII("stale");
  const base::FilePath sibling = profile.GetPath().AppendASCII("keep");
  ASSERT_TRUE(base::CreateDirectory(staging));
  ASSERT_TRUE(base::WriteFile(stale, base::byte_span_from_cstring("secret")));
  ASSERT_TRUE(base::WriteFile(sibling, base::byte_span_from_cstring("keep")));

  auto store = BackupArchiveStageStore::Create(staging);
  ASSERT_TRUE(store);
  EXPECT_FALSE(base::PathExists(stale));
  EXPECT_TRUE(base::PathExists(sibling));
  EXPECT_FALSE(BackupArchiveStageStore::Create(profile.GetPath()));
  EXPECT_TRUE(base::PathExists(sibling));
  EXPECT_TRUE(base::DirectoryExists(staging));
  store.reset();
  EXPECT_FALSE(base::PathExists(staging));
  EXPECT_TRUE(base::PathExists(sibling));
}

TEST(BackupArchiveStageStoreInitializationTest,
     CanonicalDirectoryLeaseRefusesSamePathAndParentAliasWithoutTouchingData) {
  base::ScopedTempDir root;
  ASSERT_TRUE(root.CreateUniqueTempDir());
  const base::FilePath profile = root.GetPath().AppendASCII("profile");
  const base::FilePath alias = root.GetPath().AppendASCII("profile-alias");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::CreateSymbolicLink(profile, alias));
  const base::FilePath staging = profile.Append(kBackupStagingDirectoryName);
  const base::FilePath sentinel = staging.AppendASCII("owned");

  auto store = BackupArchiveStageStore::Create(staging);
  ASSERT_TRUE(store);
  ASSERT_TRUE(base::WriteFile(sentinel, "retained"));
  EXPECT_FALSE(BackupArchiveStageStore::Create(staging));
  EXPECT_FALSE(BackupArchiveStageStore::Create(
      alias.Append(kBackupStagingDirectoryName)));
  std::string retained;
  ASSERT_TRUE(base::ReadFileToString(sentinel, &retained));
  EXPECT_EQ(retained, "retained");

  store.reset();
  EXPECT_FALSE(base::PathExists(staging));
  auto reopened = BackupArchiveStageStore::Create(
      alias.Append(kBackupStagingDirectoryName));
  EXPECT_TRUE(reopened);
}

TEST(BackupArchiveStageStoreInitializationTest,
     RefusedPathNeverAcquiresDeletionCustody) {
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  const auto staging = profile.GetPath().Append(kBackupStagingDirectoryName);
  const auto target = profile.GetPath().AppendASCII("keep");
  ASSERT_TRUE(base::WriteFile(target, "keep"));
  EXPECT_FALSE(BackupArchiveStageStore::Create(base::FilePath()));
  EXPECT_FALSE(BackupArchiveStageStore::Create(
      base::FilePath(kBackupStagingDirectoryName)));
  EXPECT_FALSE(BackupArchiveStageStore::Create(
      profile.GetPath().AppendASCII("..").Append(kBackupStagingDirectoryName)));
  ASSERT_TRUE(base::CreateSymbolicLink(target, staging));
  EXPECT_FALSE(BackupArchiveStageStore::Create(staging));
  EXPECT_TRUE(base::IsLink(staging));
  EXPECT_TRUE(base::PathExists(target));
  ASSERT_TRUE(base::DeleteFile(staging));
  ASSERT_TRUE(base::WriteFile(staging, "not a directory"));
  EXPECT_FALSE(BackupArchiveStageStore::Create(staging));
  std::string retained;
  ASSERT_TRUE(base::ReadFileToString(staging, &retained));
  EXPECT_EQ(retained, "not a directory");
  EXPECT_TRUE(base::PathExists(target));
}

}  // namespace
}  // namespace taffy::storage::backup
