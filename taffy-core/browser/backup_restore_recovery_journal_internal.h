// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_JOURNAL_INTERNAL_H_
#define TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_JOURNAL_INTERNAL_H_

#include "taffy/browser/backup_restore_recovery_journal.h"

namespace taffy::backup_restore_recovery_journal_internal {

// Structural append only. Callers own exact intent/outcome matching and the
// subsequent disk witness; portable Core owns semantic history transitions.
base::expected<void, BackupRestoreProfileRegistryError> Append(
    PrefService* local_state,
    const std::string& reservation_id,
    const core_service::mojom::BackupRestoreRecoveryRecord& record);

bool IsExactHistory(const BackupRestoreRecoveryRecords& left,
                    const BackupRestoreRecoveryRecords& right);

}  // namespace taffy::backup_restore_recovery_journal_internal

#endif  // TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_JOURNAL_INTERNAL_H_
