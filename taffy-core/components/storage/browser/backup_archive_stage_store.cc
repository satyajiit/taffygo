// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_archive_stage_store.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "crypto/secure_util.h"
#include "taffy/components/storage/browser/backup_archive_stage_store_lease.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;

constexpr size_t kMaxOutstandingStages = 4u;

bool IsAllZero(base::span<const uint8_t> bytes) {
  return std::ranges::all_of(bytes, [](uint8_t byte) { return byte == 0u; });
}

bool IsValidOperationId(const std::string& operation_id) {
  return !operation_id.empty() &&
         operation_id.size() <= mojom::kMaxOperationIdBytes &&
         std::ranges::none_of(operation_id, [](unsigned char character) {
           return character < 0x20u || character == 0x7fu;
         });
}

bool CreateStageFile(const base::FilePath& directory, base::FilePath* output) {
  return base::CreateTemporaryFileInDir(directory, output) && !output->empty();
}

base::File OpenReadOnly(const base::FilePath& path) {
  return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
}

base::File OpenEmptyWriter(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                            base::File::FLAG_WRITE);
  if (!file.IsValid() || !file.SetLength(0) ||
      file.Seek(base::File::FROM_BEGIN, 0) != 0) {
    return base::File();
  }
  return file;
}

BackupStageStatus OpenFailureStatus(AibArchiveError error) {
  switch (error) {
    case AibArchiveError::kUnreadableSource:
    case AibArchiveError::kUnwritableDestination:
      return BackupStageStatus::kUnavailable;
    case AibArchiveError::kInvalidArgument:
    case AibArchiveError::kWrongMagic:
    case AibArchiveError::kUnsupportedFormat:
    case AibArchiveError::kUnsupportedCipherSuite:
    case AibArchiveError::kMalformedHeader:
    case AibArchiveError::kManifestTooLarge:
    case AibArchiveError::kPayloadTooLarge:
    case AibArchiveError::kArchiveTooLarge:
    case AibArchiveError::kTruncated:
    case AibArchiveError::kTrailingData:
    case AibArchiveError::kNonceReuse:
    case AibArchiveError::kAuthenticationFailed:
      return BackupStageStatus::kRefused;
  }
  return BackupStageStatus::kRefused;
}

}  // namespace

struct BackupArchiveStageStore::Entry {
  enum class Kind : uint8_t { kExport, kImport };
  enum class State : uint8_t {
    kReady,
    kReadbackOpen,
    kCopying,
    kImportOpen,
    kVerified,
    kRefused,
  };

  explicit Entry(Kind kind) : kind(kind) {}
  ~Entry() {
    crypto::SecureZeroBuffer(recovery_key);
    crypto::SecureZeroBuffer(expected_manifest);
    crypto::SecureZeroBuffer(imported_manifest);
    if (!encrypted_path.empty()) {
      base::DeleteFile(encrypted_path);
    }
    if (!secondary_path.empty()) {
      base::DeleteFile(secondary_path);
    }
  }

  const Kind kind;
  State state = State::kReady;
  Secret recovery_key{};
  base::FilePath encrypted_path;
  base::FilePath secondary_path;
  uint64_t archive_bytes = 0u;
  std::vector<uint8_t> expected_manifest;
  std::vector<uint8_t> imported_manifest;
  uint64_t imported_payload_bytes = 0u;
  AibPublicHeader export_header;
};

VerifiedBackupImport::VerifiedBackupImport() = default;
VerifiedBackupImport::VerifiedBackupImport(VerifiedBackupImport&& other)
    : manifest_plaintext(std::move(other.manifest_plaintext)),
      payload_plaintext_bytes(std::exchange(other.payload_plaintext_bytes, 0u)),
      plaintext_payload(std::move(other.plaintext_payload)) {}
VerifiedBackupImport& VerifiedBackupImport::operator=(
    VerifiedBackupImport&& other) {
  if (this != &other) {
    crypto::SecureZeroBuffer(manifest_plaintext);
    manifest_plaintext = std::move(other.manifest_plaintext);
    payload_plaintext_bytes = std::exchange(other.payload_plaintext_bytes, 0u);
    plaintext_payload = std::move(other.plaintext_payload);
  }
  return *this;
}
VerifiedBackupImport::~VerifiedBackupImport() {
  crypto::SecureZeroBuffer(manifest_plaintext);
}

// static
scoped_refptr<BackupArchiveStageStore> BackupArchiveStageStore::Create(
    const base::FilePath& staging_directory) {
  auto lease = internal::BackupStageDirectoryLease::Acquire(staging_directory);
  if (!lease) {
    return nullptr;
  }
  const base::FilePath canonical_directory = lease->directory();
  auto store = base::WrapRefCounted(
      new BackupArchiveStageStore(canonical_directory, std::move(lease)));
  return store->Initialize() ? store : nullptr;
}

BackupArchiveStageStore::BackupArchiveStageStore(
    base::FilePath staging_directory,
    std::unique_ptr<internal::BackupStageDirectoryLease> directory_lease)
    : staging_directory_(std::move(staging_directory)),
      directory_lease_(std::move(directory_lease)) {}

BackupArchiveStageStore::~BackupArchiveStageStore() {
  AbandonAll();
  base::DeletePathRecursively(staging_directory_);
}

bool BackupArchiveStageStore::Initialize() {
  return (!base::PathExists(staging_directory_) ||
          base::DeletePathRecursively(staging_directory_)) &&
         base::CreateDirectory(staging_directory_);
}

base::File BackupArchiveStageStore::CreatePlaintextScratchFile() const {
  base::FilePath path;
  base::File file =
      base::CreateAndOpenTemporaryFileInDir(staging_directory_, &path);
  if (!file.IsValid() || path.empty() || !base::DeleteFile(path)) {
    file.Close();
    if (!path.empty()) {
      base::DeleteFile(path);
    }
    return base::File();
  }
  return file;
}

base::expected<PreparedBackupExport, BackupStageError>
BackupArchiveStageStore::PrepareExport(
    const std::string& operation_id,
    base::File plaintext_payload,
    uint64_t payload_plaintext_bytes,
    base::span<const uint8_t> recovery_key,
    base::span<const uint8_t> manifest_plaintext) {
  if (!IsValidOperationId(operation_id) || !plaintext_payload.IsValid() ||
      recovery_key.size() != kSecretBytes || IsAllZero(recovery_key) ||
      manifest_plaintext.empty() ||
      manifest_plaintext.size() > kMaxManifestPlaintextBytes ||
      payload_plaintext_bytes > kMaxPayloadPlaintextBytes) {
    return base::unexpected(BackupStageError::kInvalidArgument);
  }
  {
    base::AutoLock guard(lock_);
    if (entries_.contains(operation_id)) {
      return base::unexpected(BackupStageError::kAlreadyExists);
    }
    if (entries_.size() >= kMaxOutstandingStages) {
      return base::unexpected(BackupStageError::kCapacityExceeded);
    }
  }

  auto entry = std::make_unique<Entry>(Entry::Kind::kExport);
  std::ranges::copy(recovery_key, entry->recovery_key.begin());
  entry->expected_manifest.assign(manifest_plaintext.begin(),
                                  manifest_plaintext.end());
  if (!CreateStageFile(staging_directory_, &entry->encrypted_path) ||
      !CreateStageFile(staging_directory_, &entry->secondary_path)) {
    return base::unexpected(BackupStageError::kIoFailure);
  }
  base::File encrypted = OpenEmptyWriter(entry->encrypted_path);
  if (!encrypted.IsValid()) {
    return base::unexpected(BackupStageError::kIoFailure);
  }
  const AibWriteArchiveResult written =
      WriteAibArchive(&encrypted, &plaintext_payload, payload_plaintext_bytes,
                      recovery_key, manifest_plaintext);
  if (!written) {
    return base::unexpected(BackupStageError::kArchiveRefused);
  }
  const AibOpenArchiveResult locally_opened =
      OpenAibArchive(&encrypted, nullptr, recovery_key);
  if (!locally_opened || locally_opened->manifest != entry->expected_manifest ||
      locally_opened->payload_plaintext_bytes != payload_plaintext_bytes ||
      locally_opened->header != written->header) {
    return base::unexpected(BackupStageError::kArchiveRefused);
  }
  entry->archive_bytes = written->archive_bytes;
  entry->export_header = written->header;

  base::AutoLock guard(lock_);
  if (entries_.contains(operation_id)) {
    return base::unexpected(BackupStageError::kAlreadyExists);
  }
  if (entries_.size() >= kMaxOutstandingStages) {
    return base::unexpected(BackupStageError::kCapacityExceeded);
  }
  entries_.insert_or_assign(operation_id, std::move(entry));
  return PreparedBackupExport{.archive_bytes = written->archive_bytes};
}

base::File BackupArchiveStageStore::OpenExportReadback(
    const std::string& operation_id,
    uint64_t expected_archive_bytes) {
  base::AutoLock guard(lock_);
  const auto found = entries_.find(operation_id);
  if (found == entries_.end() || found->second->kind != Entry::Kind::kExport ||
      found->second->state != Entry::State::kReady ||
      expected_archive_bytes == 0u ||
      expected_archive_bytes != found->second->archive_bytes) {
    return base::File();
  }
  base::File output = OpenEmptyWriter(found->second->secondary_path);
  if (output.IsValid()) {
    found->second->state = Entry::State::kReadbackOpen;
  }
  return output;
}

base::File BackupArchiveStageStore::OpenEncryptedExport(
    const std::string& operation_id) {
  base::AutoLock guard(lock_);
  const auto found = entries_.find(operation_id);
  if (found == entries_.end() || found->second->kind != Entry::Kind::kExport ||
      found->second->state != Entry::State::kReadbackOpen) {
    return base::File();
  }
  base::File input = OpenReadOnly(found->second->encrypted_path);
  if (input.IsValid()) {
    found->second->state = Entry::State::kCopying;
  }
  return input;
}

BackupStageStatus BackupArchiveStageStore::VerifyExportReadback(
    const std::string& operation_id) {
  base::AutoLock guard(lock_);
  const auto found = entries_.find(operation_id);
  if (found == entries_.end() || found->second->kind != Entry::Kind::kExport ||
      found->second->state != Entry::State::kCopying) {
    return BackupStageStatus::kUnavailable;
  }
  Entry& entry = *found->second;
  base::File readback = OpenReadOnly(entry.secondary_path);
  if (!readback.IsValid()) {
    entry.state = Entry::State::kRefused;
    return BackupStageStatus::kUnavailable;
  }
  const int64_t length = readback.GetLength();
  if (length < 0 || static_cast<uint64_t>(length) != entry.archive_bytes) {
    entry.state = Entry::State::kRefused;
    return BackupStageStatus::kRefused;
  }
  const AibOpenArchiveResult opened =
      OpenAibArchive(&readback, nullptr, entry.recovery_key);
  if (!opened) {
    entry.state = Entry::State::kRefused;
    return OpenFailureStatus(opened.error());
  }
  if (opened->header != entry.export_header ||
      opened->manifest != entry.expected_manifest) {
    entry.state = Entry::State::kRefused;
    return BackupStageStatus::kRefused;
  }
  entry.state = Entry::State::kVerified;
  return BackupStageStatus::kVerified;
}

bool BackupArchiveStageStore::PrepareImport(
    const std::string& operation_id,
    base::span<const uint8_t> recovery_key) {
  if (!IsValidOperationId(operation_id) ||
      recovery_key.size() != kSecretBytes || IsAllZero(recovery_key)) {
    return false;
  }
  base::AutoLock guard(lock_);
  if (entries_.contains(operation_id) ||
      entries_.size() >= kMaxOutstandingStages) {
    return false;
  }
  auto entry = std::make_unique<Entry>(Entry::Kind::kImport);
  std::ranges::copy(recovery_key, entry->recovery_key.begin());
  entries_.insert_or_assign(operation_id, std::move(entry));
  return true;
}

base::File BackupArchiveStageStore::OpenEncryptedImport(
    const std::string& operation_id,
    uint64_t maximum_archive_bytes) {
  base::AutoLock guard(lock_);
  const auto found = entries_.find(operation_id);
  if (found == entries_.end() || found->second->kind != Entry::Kind::kImport ||
      found->second->state != Entry::State::kReady ||
      maximum_archive_bytes != MaximumArchiveBytes()) {
    return base::File();
  }
  if (!CreateStageFile(staging_directory_, &found->second->encrypted_path) ||
      !CreateStageFile(staging_directory_, &found->second->secondary_path)) {
    found->second->state = Entry::State::kRefused;
    return base::File();
  }
  base::File output = OpenEmptyWriter(found->second->encrypted_path);
  if (output.IsValid()) {
    found->second->state = Entry::State::kImportOpen;
  }
  return output;
}

BackupStageStatus BackupArchiveStageStore::InspectImportedArchive(
    const std::string& operation_id,
    uint64_t actual_archive_bytes) {
  base::AutoLock guard(lock_);
  const auto found = entries_.find(operation_id);
  if (found == entries_.end() || found->second->kind != Entry::Kind::kImport ||
      found->second->state != Entry::State::kImportOpen) {
    return BackupStageStatus::kUnavailable;
  }
  Entry& entry = *found->second;
  base::File encrypted = OpenReadOnly(entry.encrypted_path);
  const int64_t length = encrypted.IsValid() ? encrypted.GetLength() : -1;
  if (actual_archive_bytes == 0u ||
      actual_archive_bytes > MaximumArchiveBytes() || length < 0 ||
      static_cast<uint64_t>(length) != actual_archive_bytes) {
    entry.state = Entry::State::kRefused;
    return encrypted.IsValid() ? BackupStageStatus::kRefused
                               : BackupStageStatus::kUnavailable;
  }
  base::File plaintext = OpenEmptyWriter(entry.secondary_path);
  if (!plaintext.IsValid()) {
    entry.state = Entry::State::kRefused;
    return BackupStageStatus::kUnavailable;
  }
  AibOpenArchiveResult opened =
      OpenAibArchive(&encrypted, &plaintext, entry.recovery_key);
  if (!opened) {
    entry.state = Entry::State::kRefused;
    return OpenFailureStatus(opened.error());
  }
  entry.imported_manifest = std::move(opened->manifest);
  entry.imported_payload_bytes = opened->payload_plaintext_bytes;
  entry.archive_bytes = actual_archive_bytes;
  entry.state = Entry::State::kVerified;
  return BackupStageStatus::kVerified;
}

std::optional<VerifiedBackupImport> BackupArchiveStageStore::ReadVerifiedImport(
    const std::string& operation_id) const {
  base::AutoLock guard(lock_);
  const auto found = entries_.find(operation_id);
  if (found == entries_.end() || found->second->kind != Entry::Kind::kImport ||
      found->second->state != Entry::State::kVerified) {
    return std::nullopt;
  }
  base::File plaintext = OpenReadOnly(found->second->secondary_path);
  if (!plaintext.IsValid()) {
    return std::nullopt;
  }
  VerifiedBackupImport result;
  result.manifest_plaintext = found->second->imported_manifest;
  result.payload_plaintext_bytes = found->second->imported_payload_bytes;
  result.plaintext_payload = std::move(plaintext);
  return result;
}

void BackupArchiveStageStore::Abandon(const std::string& operation_id) {
  base::AutoLock guard(lock_);
  entries_.erase(operation_id);
}

void BackupArchiveStageStore::AbandonAll() {
  base::AutoLock guard(lock_);
  entries_.clear();
}

size_t BackupArchiveStageStore::size_for_testing() const {
  base::AutoLock guard(lock_);
  return entries_.size();
}

}  // namespace taffy::storage::backup
