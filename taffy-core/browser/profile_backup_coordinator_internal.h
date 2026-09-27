// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_INTERNAL_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_INTERNAL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/types/expected.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"
#include "taffy/components/storage/browser/backup_storage_snapshot.h"

namespace taffy {

struct SensitiveBackupSecret {
  storage::backup::Secret bytes{};

  SensitiveBackupSecret();
  explicit SensitiveBackupSecret(storage::backup::Secret source);
  SensitiveBackupSecret(const SensitiveBackupSecret&) = delete;
  SensitiveBackupSecret& operator=(const SensitiveBackupSecret&) = delete;
  SensitiveBackupSecret(SensitiveBackupSecret&&);
  SensitiveBackupSecret& operator=(SensitiveBackupSecret&&);
  ~SensitiveBackupSecret();
};

struct ValidatedBackupSnapshot {
  std::vector<storage::backup::BackupSnapshotRecord> records;
  std::vector<core_service::mojom::BackupRecordDescriptorPtr> descriptors;
  uint64_t payload_plaintext_bytes = 0;

  ValidatedBackupSnapshot();
  ValidatedBackupSnapshot(ValidatedBackupSnapshot&&);
  ValidatedBackupSnapshot& operator=(ValidatedBackupSnapshot&&);
  ~ValidatedBackupSnapshot();
};

struct VerifiedBackupImportPayload {
  storage::backup::VerifiedBackupImport verified;

  VerifiedBackupImportPayload();
  VerifiedBackupImportPayload(VerifiedBackupImportPayload&&);
  VerifiedBackupImportPayload& operator=(VerifiedBackupImportPayload&&);
  ~VerifiedBackupImportPayload();
};

struct HashedBackupImport {
  storage::backup::VerifiedBackupImport verified;
  std::vector<core_service::mojom::StagedBackupRecordPtr> staged_records;

  HashedBackupImport();
  HashedBackupImport(HashedBackupImport&&);
  HashedBackupImport& operator=(HashedBackupImport&&);
  ~HashedBackupImport();
};

bool IsValidProfileBackupOperationId(std::string_view operation_id);
core_service::mojom::OperationEnvelopePtr NewBackupOperation(
    uint64_t service_generation,
    std::string_view kind,
    std::string_view public_operation_id);
bool IsSupportedBackupManifestSelection(
    base::span<const core_service::mojom::BackupRecordKind> selection);

base::expected<ValidatedBackupSnapshot, ProfileBackupError>
ValidateBackupSnapshot(
    storage::backup::BackupSnapshotResult snapshot,
    base::span<const core_service::mojom::BackupRecordKind> selection);

base::expected<PreparedProfileBackupExport, ProfileBackupError>
SealValidatedBackupSnapshot(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    std::string operation_id,
    SensitiveBackupSecret recovery_key,
    std::vector<uint8_t> manifest_plaintext,
    std::array<uint8_t, 32> snapshot_sha256,
    std::vector<uint32_t> source_order,
    uint64_t expected_payload_plaintext_bytes,
    ValidatedBackupSnapshot snapshot);

base::expected<VerifiedBackupImportPayload, ProfileBackupError>
ReadVerifiedBackupImport(
    scoped_refptr<storage::backup::BackupArchiveStageStore> stage_store,
    std::string operation_id);

base::expected<HashedBackupImport, ProfileBackupError> HashBackupImportPayload(
    VerifiedBackupImportPayload imported,
    const core_service::mojom::BackupManifestInspectResult& inspection);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_INTERNAL_H_
