// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_CODEC_H_
#define TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_CODEC_H_

#include <string_view>
#include <vector>

#include "base/types/expected.h"
#include "base/values.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

inline constexpr char kBackupRestoreRecoveryJournalKey[] = "recovery_journal";

using BackupRestoreRecoveryRecords =
    std::vector<core_service::mojom::BackupRestoreRecoveryRecordPtr>;

// Exact content-free persistence codec for the generated contract. Unknown
// fields/enums, noncanonical integers/digests, noncontiguous sequence numbers
// and changed binding are refused. This codec does NOT reduce restore policy:
// the source Core's read-only recovery inspector owns semantic classification.
// A structurally valid history still grants no physical authority.
base::expected<BackupRestoreRecoveryRecords, BackupRestoreProfileRegistryError>
DecodeBackupRestoreRecoveryJournal(const base::Value& value,
                                   std::string_view reservation_id,
                                   std::string_view target_profile_id);

base::expected<base::DictValue, BackupRestoreProfileRegistryError>
EncodeBackupRestoreRecoveryRecord(
    const core_service::mojom::BackupRestoreRecoveryRecord& record);

namespace backup_restore_recovery_codec {

base::expected<base::DictValue, BackupRestoreProfileRegistryError>
EncodeBinding(const core_service::mojom::BackupRestoreRecoveryBinding& binding);
core_service::mojom::BackupRestoreRecoveryBindingPtr DecodeBinding(
    const base::DictValue& value);

}  // namespace backup_restore_recovery_codec
}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_CODEC_H_
