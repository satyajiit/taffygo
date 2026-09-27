// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_JOURNAL_H_
#define TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_JOURNAL_H_

#include <string>

#include "taffy/browser/backup_restore_recovery_codec.h"

namespace taffy {

using BackupRestoreRecoveryRecordResult =
    base::expected<core_service::mojom::BackupRestoreRecoveryRecordPtr,
                   BackupRestoreProfileRegistryError>;

// Absence is an empty precommit history. A present malformed/empty value is
// corrupt and cannot remove quarantine. Semantic history classification is
// exclusively the sandboxed Core's read-only inspection operation.
base::expected<BackupRestoreRecoveryRecords, BackupRestoreProfileRegistryError>
ReadBackupRestoreRecoveryJournal(const PrefService* local_state,
                                 const std::string& reservation_id);

// These calls modify the in-memory preference store only. The physical owner
// MUST immediately capture the complete registry value, drain its pending
// write, and establish exact synchronized file readback before dispatch or
// reporting a durable observation. Neither a returned record nor a preference
// callback alone proves persistence. No helper starts, retries, publishes or
// deletes a target; a repeated begin is refused even for the same identity.
BackupRestoreRecoveryRecordResult BeginBackupRestoreCommitIntent(
    PrefService* local_state,
    const std::string& reservation_id,
    const core_service::mojom::BackupRestoreBinding& binding,
    const core_service::mojom::BackupRestoreCandidateWitness& witness,
    std::string intent_id);

// Appends only an observation of this exact initial commit intent. An unknown
// observation may later be settled by physical reconciliation, never replay.
// Repeating the exact terminal observation returns its existing record so the
// caller can retry the persistence witness, not the physical action.
BackupRestoreRecoveryRecordResult RecordBackupRestoreCommitOutcome(
    PrefService* local_state,
    const std::string& reservation_id,
    const core_service::mojom::BackupRestoreRecoveryRecord& exact_intent,
    core_service::mojom::BackupRestoreCommitOutcome outcome);

// Consumes the exact prefix carried by the current source Core's resolution
// authorization. The caller must check that authorization against the live
// source generation and clock immediately before this call and again before
// physical work. This helper verifies transport shape and exact persisted
// identity, not the semantic choice. Repeating begin never authorizes replay.
BackupRestoreRecoveryRecordResult BeginBackupRestoreResolutionIntent(
    PrefService* local_state,
    const std::string& reservation_id,
    const core_service::mojom::BackupRestoreRecoveryResolutionAuthorization&
        authorization);

// Records an observation of the exact already-journaled accept/discard
// intent, not permission to perform it. An unknown suffix can be settled once;
// a conflicting definitive outcome or any intervening intent is refused.
BackupRestoreRecoveryRecordResult RecordBackupRestoreResolutionOutcome(
    PrefService* local_state,
    const std::string& reservation_id,
    const core_service::mojom::BackupRestoreRecoveryRecord& exact_intent,
    core_service::mojom::BackupRestoreResolutionOutcome outcome);

}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_RESTORE_RECOVERY_JOURNAL_H_
