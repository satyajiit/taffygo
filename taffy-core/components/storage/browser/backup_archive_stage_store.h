// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_BACKUP_ARCHIVE_STAGE_STORE_H_
#define TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_BACKUP_ARCHIVE_STAGE_STORE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/types/expected.h"
#include "taffy/components/storage/browser/encrypted_backup_archive.h"

namespace taffy::storage::backup {

namespace internal {
class BackupStageDirectoryLease;
}

inline constexpr base::FilePath::CharType kBackupStagingDirectoryName[] =
    FILE_PATH_LITERAL("TaffyBackupStaging");

enum class BackupStageError : uint8_t {
  kInvalidArgument,
  kAlreadyExists,
  kCapacityExceeded,
  kIoFailure,
  kArchiveRefused,
};

enum class BackupStageStatus : uint8_t {
  kVerified,
  kRefused,
  kUnavailable,
};

struct PreparedBackupExport {
  uint64_t archive_bytes = 0;
};

struct VerifiedBackupImport {
  std::vector<uint8_t> manifest_plaintext;
  uint64_t payload_plaintext_bytes = 0;
  base::File plaintext_payload;

  VerifiedBackupImport();
  VerifiedBackupImport(VerifiedBackupImport&&);
  VerifiedBackupImport& operator=(VerifiedBackupImport&&);
  ~VerifiedBackupImport();
};

// Owns encrypted and plaintext temporary files for one regular profile. The
// caller supplies the exact dedicated profile child named above; Create()
// clears only that child, removing stages left by process death before a new
// operation can begin. Create() canonicalizes the existing profile parent and
// refuses while any store reference owns that directory, including detached
// Android I/O and queued cleanup references.
//
// It is safe to call the operation methods from Android's I/O dispatcher.
// Destruction must happen only after callers stop using it. Record payloads,
// manifests and recovery keys never cross its Android FD methods.
class BackupArchiveStageStore
    : public base::RefCountedThreadSafe<BackupArchiveStageStore> {
 public:
  static scoped_refptr<BackupArchiveStageStore> Create(
      const base::FilePath& staging_directory);

  BackupArchiveStageStore(const BackupArchiveStageStore&) = delete;
  BackupArchiveStageStore& operator=(const BackupArchiveStageStore&) = delete;
  // Seals and locally authenticates a Core-prepared manifest plus one exact
  // immutable plaintext payload file before making the archive available to
  // Android's document writer.
  base::expected<PreparedBackupExport, BackupStageError> PrepareExport(
      const std::string& operation_id,
      base::File plaintext_payload,
      uint64_t payload_plaintext_bytes,
      base::span<const uint8_t> recovery_key,
      base::span<const uint8_t> manifest_plaintext);

  // Creates one native-only plaintext scratch file inside the dedicated stage
  // directory and unlinks its name before returning it. The caller may fill
  // and rewind the handle on a blocking sequence, then move it to
  // PrepareExport(). No plaintext path becomes observable to Android.
  base::File CreatePlaintextScratchFile() const;

  // Export FD order is intentional: Android opens the empty readback stage
  // first (validating expected length before changing provider data), then
  // opens the encrypted source. Returned files are detached operation-owned
  // handles; callers close them before VerifyExportReadback().
  base::File OpenExportReadback(const std::string& operation_id,
                                uint64_t expected_archive_bytes);
  base::File OpenEncryptedExport(const std::string& operation_id);
  BackupStageStatus VerifyExportReadback(const std::string& operation_id);

  // Import begins only after trusted native code has associated the operation
  // with its recovery authority. The Android writer receives an encrypted
  // destination only. Successful inspection retains the authenticated
  // manifest and plaintext staging file for the asynchronous Core planner.
  bool PrepareImport(const std::string& operation_id,
                     base::span<const uint8_t> recovery_key);
  base::File OpenEncryptedImport(const std::string& operation_id,
                                 uint64_t maximum_archive_bytes);
  BackupStageStatus InspectImportedArchive(const std::string& operation_id,
                                           uint64_t actual_archive_bytes);
  std::optional<VerifiedBackupImport> ReadVerifiedImport(
      const std::string& operation_id) const;

  // Idempotent, nonthrowing cleanup. Erasure closes native state, wipes key
  // and manifest memory, and removes every exact temporary path.
  void Abandon(const std::string& operation_id);
  void AbandonAll();

  static constexpr uint64_t MaximumArchiveBytes() {
    return kMaxAibArchiveBytes;
  }

  size_t size_for_testing() const;

 private:
  friend class base::RefCountedThreadSafe<BackupArchiveStageStore>;

  struct Entry;

  BackupArchiveStageStore(
      base::FilePath staging_directory,
      std::unique_ptr<internal::BackupStageDirectoryLease> directory_lease);
  ~BackupArchiveStageStore();
  bool Initialize();

  const base::FilePath staging_directory_;
  const std::unique_ptr<internal::BackupStageDirectoryLease> directory_lease_;
  mutable base::Lock lock_;
  base::flat_map<std::string, std::unique_ptr<Entry>> entries_
      GUARDED_BY(lock_);
};

}  // namespace taffy::storage::backup

#endif  // TAFFY_CORE_COMPONENTS_STORAGE_BROWSER_BACKUP_ARCHIVE_STAGE_STORE_H_
