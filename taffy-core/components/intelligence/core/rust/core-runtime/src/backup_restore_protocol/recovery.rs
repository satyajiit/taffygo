// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Read-only reduction of content-free durable physical recovery facts.

use taffy_storage::backup::{
    validate_restore_recovery_history, BackupRecordKind, RestoreObservedOutcome,
    RestorePhysicalIntent, RestoreRecoveryBinding, RestoreRecoveryError, RestoreRecoveryFact,
    RestoreRecoveryRecord, RestoreRecoveryStatus, RestoreTargetKind,
    MAX_BACKUP_RESTORE_RECOVERY_RECORDS,
};

use super::BackupRestoreProtocol;
use crate::backup_planning::valid_operation;
use crate::wire;

impl BackupRestoreProtocol {
    /// Classifies durable physical facts without mutating the retained restore.
    ///
    /// A successful result is observational only. In particular,
    /// `ReconcileRequired` and `CleanupRequired` are never permissions to
    /// repeat work, publish a target, or delete one.
    pub fn inspect_recovery(
        &self,
        request: wire::BackupRestoreRecoveryInspectionRequest,
        now_monotonic_ms: u64,
    ) -> wire::BackupRestoreRecoveryInspectionResult {
        let operation = request.operation;
        if !self.connected || self.private_profile {
            return result(
                operation,
                wire::BackupRestoreRecoveryInspectionStatus::Unavailable,
                None,
                None,
            );
        }
        if !valid_operation(&operation, self.generation, now_monotonic_ms) {
            return result(
                operation,
                wire::BackupRestoreRecoveryInspectionStatus::InvalidOperation,
                None,
                None,
            );
        }
        if request.records.len() > MAX_BACKUP_RESTORE_RECOVERY_RECORDS {
            return invalid_history(operation, RestoreRecoveryError::TooManyRecords);
        }
        if request
            .records
            .iter()
            .any(|record| record.binding.owner_profile_id != self.owner_profile_id)
        {
            return result(
                operation,
                wire::BackupRestoreRecoveryInspectionStatus::InvalidRecord,
                None,
                None,
            );
        }
        let Some(records) = records_from_wire(&request.records) else {
            return result(
                operation,
                wire::BackupRestoreRecoveryInspectionStatus::InvalidRecord,
                None,
                None,
            );
        };
        match validate_restore_recovery_history(&records) {
            Ok(status) => result(
                operation,
                wire::BackupRestoreRecoveryInspectionStatus::Succeeded,
                Some(classification_to_wire(status)),
                None,
            ),
            Err(error) => invalid_history(operation, error),
        }
    }
}

pub(super) fn records_from_wire(
    input: &[wire::BackupRestoreRecoveryRecord],
) -> Option<Vec<RestoreRecoveryRecord>> {
    input.iter().map(record_from_wire).collect()
}

fn record_from_wire(input: &wire::BackupRestoreRecoveryRecord) -> Option<RestoreRecoveryRecord> {
    if !input.has_valid_body() {
        return None;
    }
    let binding = binding_from_wire(&input.binding);
    let fact = match input.fact_kind {
        wire::BackupRestoreRecoveryFactKind::IntentRecorded => {
            let fact = input.intent.as_ref()?;
            RestoreRecoveryFact::IntentRecorded {
                intent_id: fact.intent_id.clone(),
                intent: intent_from_wire(fact.intent),
            }
        }
        wire::BackupRestoreRecoveryFactKind::OutcomeObserved => {
            let fact = input.outcome.as_ref()?;
            RestoreRecoveryFact::OutcomeObserved {
                intent_id: fact.intent_id.clone(),
                outcome: outcome_from_wire(fact.outcome),
            }
        }
    };
    Some(RestoreRecoveryRecord {
        format_version: input.format_version,
        sequence: input.sequence,
        binding,
        fact,
    })
}

pub(super) fn binding_from_wire(
    input: &wire::BackupRestoreRecoveryBinding,
) -> RestoreRecoveryBinding {
    RestoreRecoveryBinding {
        reservation_id: input.reservation_id.clone(),
        owner_profile_id: input.owner_profile_id.clone(),
        target_kind: match input.target_kind {
            wire::BackupRestoreTargetKind::NewRegularProfile => {
                RestoreTargetKind::NewRegularProfile
            }
            wire::BackupRestoreTargetKind::ExistingRegularProfile => {
                RestoreTargetKind::ExistingRegularProfile
            }
        },
        target_profile_id: input.target_profile_id.clone(),
        backup_id: input.backup_id.clone(),
        snapshot_sha256: input.snapshot_sha256,
        confirmation_sha256: input.confirmation_sha256,
        selection: input
            .selection
            .iter()
            .copied()
            .map(kind_from_wire)
            .collect(),
        record_count: input.record_count,
        candidate_records_sha256: input.candidate_records_sha256,
    }
}

const fn kind_from_wire(kind: wire::BackupRecordKind) -> BackupRecordKind {
    match kind {
        wire::BackupRecordKind::AssistantConfiguration => BackupRecordKind::AssistantConfiguration,
        wire::BackupRecordKind::SavedWorkspace => BackupRecordKind::SavedWorkspace,
        wire::BackupRecordKind::LibraryEntry => BackupRecordKind::LibraryEntry,
        wire::BackupRecordKind::MemoryRecord => BackupRecordKind::MemoryRecord,
        wire::BackupRecordKind::UserAuthoredSkill => BackupRecordKind::UserAuthoredSkill,
        wire::BackupRecordKind::LearnedProcedure => BackupRecordKind::LearnedProcedure,
        wire::BackupRecordKind::Bookmark => BackupRecordKind::Bookmark,
        wire::BackupRecordKind::BrowserPreference => BackupRecordKind::BrowserPreference,
    }
}

const fn intent_from_wire(intent: wire::BackupRestorePhysicalIntent) -> RestorePhysicalIntent {
    match intent {
        wire::BackupRestorePhysicalIntent::CommitCandidate => {
            RestorePhysicalIntent::CommitCandidate
        }
        wire::BackupRestorePhysicalIntent::AcceptCandidate => {
            RestorePhysicalIntent::AcceptCandidate
        }
        wire::BackupRestorePhysicalIntent::DiscardCandidate => {
            RestorePhysicalIntent::DiscardCandidate
        }
    }
}

const fn outcome_from_wire(outcome: wire::BackupRestoreObservedOutcome) -> RestoreObservedOutcome {
    match outcome {
        wire::BackupRestoreObservedOutcome::Completed => RestoreObservedOutcome::Completed,
        wire::BackupRestoreObservedOutcome::DefinitelyNotCompleted => {
            RestoreObservedOutcome::DefinitelyNotCompleted
        }
        wire::BackupRestoreObservedOutcome::OutcomeUnknown => {
            RestoreObservedOutcome::OutcomeUnknown
        }
    }
}

fn classification_to_wire(
    status: RestoreRecoveryStatus,
) -> wire::BackupRestoreRecoveryClassification {
    let (kind, reconciliation) = match status {
        RestoreRecoveryStatus::ReconcileRequired { intent_id, intent } => (
            wire::BackupRestoreRecoveryClassificationKind::ReconcileRequired,
            Some(wire::BackupRestoreRecoveryReconciliation {
                intent_id,
                intent: intent_to_wire(intent),
            }),
        ),
        RestoreRecoveryStatus::RollbackAvailable => (
            wire::BackupRestoreRecoveryClassificationKind::RollbackAvailable,
            None,
        ),
        RestoreRecoveryStatus::CleanupRequired => (
            wire::BackupRestoreRecoveryClassificationKind::CleanupRequired,
            None,
        ),
        RestoreRecoveryStatus::Published => (
            wire::BackupRestoreRecoveryClassificationKind::Published,
            None,
        ),
        RestoreRecoveryStatus::VerifiedDeleted => (
            wire::BackupRestoreRecoveryClassificationKind::VerifiedDeleted,
            None,
        ),
    };
    wire::BackupRestoreRecoveryClassification {
        kind,
        reconciliation,
    }
}

const fn intent_to_wire(intent: RestorePhysicalIntent) -> wire::BackupRestorePhysicalIntent {
    match intent {
        RestorePhysicalIntent::CommitCandidate => {
            wire::BackupRestorePhysicalIntent::CommitCandidate
        }
        RestorePhysicalIntent::AcceptCandidate => {
            wire::BackupRestorePhysicalIntent::AcceptCandidate
        }
        RestorePhysicalIntent::DiscardCandidate => {
            wire::BackupRestorePhysicalIntent::DiscardCandidate
        }
    }
}

const fn error_to_wire(error: RestoreRecoveryError) -> wire::BackupRestoreRecoveryError {
    match error {
        RestoreRecoveryError::MissingCommitIntent => {
            wire::BackupRestoreRecoveryError::MissingCommitIntent
        }
        RestoreRecoveryError::TooManyRecords => wire::BackupRestoreRecoveryError::TooManyRecords,
        RestoreRecoveryError::UnsupportedVersion => {
            wire::BackupRestoreRecoveryError::UnsupportedVersion
        }
        RestoreRecoveryError::InvalidSequence => wire::BackupRestoreRecoveryError::InvalidSequence,
        RestoreRecoveryError::InvalidBinding => wire::BackupRestoreRecoveryError::InvalidBinding,
        RestoreRecoveryError::BindingChanged => wire::BackupRestoreRecoveryError::BindingChanged,
        RestoreRecoveryError::InvalidIntentId => wire::BackupRestoreRecoveryError::InvalidIntentId,
        RestoreRecoveryError::IntentIdReused => wire::BackupRestoreRecoveryError::IntentIdReused,
        RestoreRecoveryError::UnexpectedIntent => {
            wire::BackupRestoreRecoveryError::UnexpectedIntent
        }
        RestoreRecoveryError::UnresolvedIntent => {
            wire::BackupRestoreRecoveryError::UnresolvedIntent
        }
        RestoreRecoveryError::OutcomeWithoutIntent => {
            wire::BackupRestoreRecoveryError::OutcomeWithoutIntent
        }
        RestoreRecoveryError::OutcomeIntentMismatch => {
            wire::BackupRestoreRecoveryError::OutcomeIntentMismatch
        }
        RestoreRecoveryError::DuplicateUnknownOutcome => {
            wire::BackupRestoreRecoveryError::DuplicateUnknownOutcome
        }
        RestoreRecoveryError::TerminalHistoryExtended => {
            wire::BackupRestoreRecoveryError::TerminalHistoryExtended
        }
    }
}

fn invalid_history(
    operation: wire::OperationEnvelope,
    error: RestoreRecoveryError,
) -> wire::BackupRestoreRecoveryInspectionResult {
    result(
        operation,
        wire::BackupRestoreRecoveryInspectionStatus::InvalidHistory,
        None,
        Some(wire::BackupRestoreRecoveryFailure {
            error: error_to_wire(error),
        }),
    )
}

fn result(
    operation: wire::OperationEnvelope,
    status: wire::BackupRestoreRecoveryInspectionStatus,
    classification: Option<wire::BackupRestoreRecoveryClassification>,
    failure: Option<wire::BackupRestoreRecoveryFailure>,
) -> wire::BackupRestoreRecoveryInspectionResult {
    wire::BackupRestoreRecoveryInspectionResult {
        operation,
        status,
        classification,
        failure,
    }
}
