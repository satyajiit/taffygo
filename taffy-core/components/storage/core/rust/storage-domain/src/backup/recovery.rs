// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Content-free recovery facts for an issued physical restore action.
//!
//! These records are observations, not executable commands. In particular, a
//! durable intent with no outcome never permits replay: after process loss it
//! requires reconciliation of the exact physical action. The browser remains
//! the owner of quarantine, publication, deletion, and every filesystem path.

use std::collections::BTreeSet;

use super::{
    validate_id, BackupRecordKind, RestoreTargetKind, MAX_BACKUP_RECORDS,
    MAX_BACKUP_RESTORE_RECOVERY_RECORDS,
};

/// Version of the content-free recovery record shape.
pub const RESTORE_RECOVERY_RECORD_VERSION: u32 = 1;

/// Immutable identity shared by every fact about one restore.
///
/// `reservation_id` is an opaque browser-owned aggregate identity. It is not
/// a profile path or a portable authorization. The original operation
/// generation and deadline are deliberately absent: neither can authorize a
/// physical replay after restart.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RestoreRecoveryBinding {
    pub reservation_id: String,
    pub owner_profile_id: String,
    pub target_kind: RestoreTargetKind,
    pub target_profile_id: String,
    pub backup_id: String,
    pub snapshot_sha256: [u8; 32],
    pub confirmation_sha256: [u8; 32],
    /// Canonical distinct kinds present in the projected candidate records.
    /// This is not the original archive selection.
    pub selection: Vec<BackupRecordKind>,
    pub record_count: u64,
    pub candidate_records_sha256: [u8; 32],
}

/// The exact physical action whose intent was made durable before dispatch.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RestorePhysicalIntent {
    CommitCandidate,
    AcceptCandidate,
    DiscardCandidate,
}

/// A physical owner's observation, never an inference from process state.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RestoreObservedOutcome {
    /// The exact intent completed and the physical owner verified its decided
    /// postcondition. For discard this includes verified absence.
    Completed,
    /// The physical owner proved that the exact intent did not complete.
    DefinitelyNotCompleted,
    /// The physical owner cannot prove either result. This never means retry.
    OutcomeUnknown,
}

/// One append-only fact in a restore recovery history.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RestoreRecoveryFact {
    IntentRecorded {
        intent_id: String,
        intent: RestorePhysicalIntent,
    },
    OutcomeObserved {
        intent_id: String,
        outcome: RestoreObservedOutcome,
    },
}

/// One durable row. Bindings repeat so a partial or cross-restore history is
/// refused without consulting ambient profile state.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RestoreRecoveryRecord {
    pub format_version: u32,
    pub sequence: u64,
    pub binding: RestoreRecoveryBinding,
    pub fact: RestoreRecoveryFact,
}

/// The only conclusions a recovered history can establish.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RestoreRecoveryStatus {
    /// The target stays quarantined while the physical owner observes the
    /// named intent. This status grants no permission to repeat it.
    ReconcileRequired {
        intent_id: String,
        intent: RestorePhysicalIntent,
    },
    /// A committed candidate stays hidden until a fresh portable resolution.
    RollbackAvailable,
    /// Commit was proven not to complete. The reservation remains quarantined
    /// until its authorized owner performs and verifies cleanup.
    CleanupRequired,
    /// Accept was proven complete, including the publication durability gate.
    Published,
    /// Discard was proven complete, including verified target absence.
    VerifiedDeleted,
}

impl RestoreRecoveryStatus {
    /// Whether the browser must keep the reserved target hidden.
    #[must_use]
    pub const fn requires_quarantine(&self) -> bool {
        matches!(
            self,
            Self::ReconcileRequired { .. } | Self::RollbackAvailable | Self::CleanupRequired
        )
    }
}

/// Why a persisted history cannot be trusted as one restore aggregate.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RestoreRecoveryError {
    MissingCommitIntent,
    TooManyRecords,
    UnsupportedVersion,
    InvalidSequence,
    InvalidBinding,
    BindingChanged,
    InvalidIntentId,
    IntentIdReused,
    UnexpectedIntent,
    UnresolvedIntent,
    OutcomeWithoutIntent,
    OutcomeIntentMismatch,
    DuplicateUnknownOutcome,
    TerminalHistoryExtended,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum SettledState {
    AwaitingCommit,
    RollbackAvailable,
    CleanupRequired,
    Published,
    VerifiedDeleted,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum DefinitiveOutcome {
    Completed,
    DefinitelyNotCompleted,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct ActiveIntent<'a> {
    id: &'a str,
    kind: RestorePhysicalIntent,
    prior_state: SettledState,
    unknown_observed: bool,
}

/// Validates and reduces an append-only durable recovery history.
///
/// The function never returns an execute/retry result. An intent lacking a
/// definitive observation, including one followed by `OutcomeUnknown`, can
/// only produce [`RestoreRecoveryStatus::ReconcileRequired`].
/// The checks every record owes before its fact is read: the record's own
/// version, its position in the append-only sequence, its binding, and that the
/// history it extends has not already settled. Split out of
/// [`validate_restore_recovery_history`] only because that reducer is over the
/// hundred-line cap; the order of these checks is part of the contract, because
/// a record that fails an earlier one must not be read for a later one.
fn validate_record_header(
    record: &RestoreRecoveryRecord,
    expected_sequence: u64,
    expected_binding: &RestoreRecoveryBinding,
    state: SettledState,
) -> Result<(), RestoreRecoveryError> {
    if record.format_version != RESTORE_RECOVERY_RECORD_VERSION {
        return Err(RestoreRecoveryError::UnsupportedVersion);
    }
    if record.sequence != expected_sequence {
        return Err(RestoreRecoveryError::InvalidSequence);
    }
    validate_binding(&record.binding)?;
    if record.binding != *expected_binding {
        return Err(RestoreRecoveryError::BindingChanged);
    }
    if matches!(
        state,
        SettledState::Published | SettledState::VerifiedDeleted
    ) {
        return Err(RestoreRecoveryError::TerminalHistoryExtended);
    }
    Ok(())
}

pub fn validate_restore_recovery_history(
    records: &[RestoreRecoveryRecord],
) -> Result<RestoreRecoveryStatus, RestoreRecoveryError> {
    let Some(first) = records.first() else {
        return Err(RestoreRecoveryError::MissingCommitIntent);
    };
    if records.len() > MAX_BACKUP_RESTORE_RECOVERY_RECORDS {
        return Err(RestoreRecoveryError::TooManyRecords);
    }
    validate_binding(&first.binding)?;

    let expected_binding = &first.binding;
    let mut expected_sequence = 1_u64;
    let mut state = SettledState::AwaitingCommit;
    let mut active = None;
    let mut used_intent_ids = BTreeSet::new();

    for record in records {
        validate_record_header(record, expected_sequence, expected_binding, state)?;
        expected_sequence = expected_sequence
            .checked_add(1)
            .ok_or(RestoreRecoveryError::InvalidSequence)?;

        match &record.fact {
            RestoreRecoveryFact::IntentRecorded { intent_id, intent } => {
                validate_intent_id(intent_id)?;
                if active.is_some() {
                    return Err(RestoreRecoveryError::UnresolvedIntent);
                }
                if !used_intent_ids.insert(intent_id.as_str()) {
                    return Err(RestoreRecoveryError::IntentIdReused);
                }
                let permitted = matches!(
                    (state, intent),
                    (
                        SettledState::AwaitingCommit,
                        RestorePhysicalIntent::CommitCandidate
                    ) | (
                        SettledState::RollbackAvailable,
                        RestorePhysicalIntent::AcceptCandidate
                            | RestorePhysicalIntent::DiscardCandidate
                    ) | (
                        SettledState::CleanupRequired,
                        RestorePhysicalIntent::DiscardCandidate
                    )
                );
                if !permitted {
                    return Err(RestoreRecoveryError::UnexpectedIntent);
                }
                active = Some(ActiveIntent {
                    id: intent_id,
                    kind: *intent,
                    prior_state: state,
                    unknown_observed: false,
                });
            }
            RestoreRecoveryFact::OutcomeObserved { intent_id, outcome } => {
                validate_intent_id(intent_id)?;
                let Some(current) = active.as_mut() else {
                    return Err(RestoreRecoveryError::OutcomeWithoutIntent);
                };
                if current.id != intent_id {
                    return Err(RestoreRecoveryError::OutcomeIntentMismatch);
                }
                let definitive = match outcome {
                    RestoreObservedOutcome::OutcomeUnknown => {
                        if current.unknown_observed {
                            return Err(RestoreRecoveryError::DuplicateUnknownOutcome);
                        }
                        current.unknown_observed = true;
                        continue;
                    }
                    RestoreObservedOutcome::Completed => DefinitiveOutcome::Completed,
                    RestoreObservedOutcome::DefinitelyNotCompleted => {
                        DefinitiveOutcome::DefinitelyNotCompleted
                    }
                };
                state = settle(current.prior_state, current.kind, definitive);
                active = None;
            }
        }
    }

    if let Some(current) = active {
        return Ok(RestoreRecoveryStatus::ReconcileRequired {
            intent_id: current.id.to_owned(),
            intent: current.kind,
        });
    }
    match state {
        SettledState::AwaitingCommit => Err(RestoreRecoveryError::MissingCommitIntent),
        SettledState::RollbackAvailable => Ok(RestoreRecoveryStatus::RollbackAvailable),
        SettledState::CleanupRequired => Ok(RestoreRecoveryStatus::CleanupRequired),
        SettledState::Published => Ok(RestoreRecoveryStatus::Published),
        SettledState::VerifiedDeleted => Ok(RestoreRecoveryStatus::VerifiedDeleted),
    }
}

// One row per transition, deliberately. Two pairs of rows share a result, and
// merging them — which is all `match_same_arms` can suggest — would join
// transitions that have nothing to do with each other: a commit that succeeded
// with an accept that did not, and a commit that failed with a discard that
// did. The table is the specification, and its shape is what makes a missing
// transition visible.
#[allow(clippy::match_same_arms)]
fn settle(
    prior_state: SettledState,
    intent: RestorePhysicalIntent,
    outcome: DefinitiveOutcome,
) -> SettledState {
    match (prior_state, intent, outcome) {
        (
            SettledState::AwaitingCommit,
            RestorePhysicalIntent::CommitCandidate,
            DefinitiveOutcome::Completed,
        ) => SettledState::RollbackAvailable,
        (
            SettledState::AwaitingCommit,
            RestorePhysicalIntent::CommitCandidate,
            DefinitiveOutcome::DefinitelyNotCompleted,
        ) => SettledState::CleanupRequired,
        (
            SettledState::RollbackAvailable,
            RestorePhysicalIntent::AcceptCandidate,
            DefinitiveOutcome::Completed,
        ) => SettledState::Published,
        (
            SettledState::RollbackAvailable | SettledState::CleanupRequired,
            RestorePhysicalIntent::DiscardCandidate,
            DefinitiveOutcome::Completed,
        ) => SettledState::VerifiedDeleted,
        (
            SettledState::RollbackAvailable,
            RestorePhysicalIntent::AcceptCandidate | RestorePhysicalIntent::DiscardCandidate,
            DefinitiveOutcome::DefinitelyNotCompleted,
        ) => SettledState::RollbackAvailable,
        (
            SettledState::CleanupRequired,
            RestorePhysicalIntent::DiscardCandidate,
            DefinitiveOutcome::DefinitelyNotCompleted,
        ) => SettledState::CleanupRequired,
        _ => unreachable!("intent admission constrains settled transitions"),
    }
}

fn validate_binding(binding: &RestoreRecoveryBinding) -> Result<(), RestoreRecoveryError> {
    let valid_ids = validate_id("restore reservation id", &binding.reservation_id).is_ok()
        && validate_id("restore owner profile id", &binding.owner_profile_id).is_ok()
        && validate_id("restore target profile id", &binding.target_profile_id).is_ok()
        && validate_id("restore backup id", &binding.backup_id).is_ok();
    let record_count = usize::try_from(binding.record_count).ok();
    // Strictly increasing, and without indexing: `pair[0]` on a window is an
    // index the workspace's `indexing_slicing` deny rejects even though the
    // window's length is known.
    let selection_is_canonical = binding
        .selection
        .windows(2)
        .all(|pair| matches!(pair, [earlier, later] if earlier < later));
    let witness_shape_is_valid = match record_count {
        Some(0) => binding.selection.is_empty(),
        Some(count) => {
            !binding.selection.is_empty()
                && count >= binding.selection.len()
                && count <= MAX_BACKUP_RECORDS
        }
        None => false,
    };
    if !valid_ids
        || binding.target_kind != RestoreTargetKind::NewRegularProfile
        || binding.owner_profile_id == binding.target_profile_id
        || binding.snapshot_sha256.iter().all(|byte| *byte == 0)
        || binding.confirmation_sha256.iter().all(|byte| *byte == 0)
        || binding
            .candidate_records_sha256
            .iter()
            .all(|byte| *byte == 0)
        || binding.selection.len() > BackupRecordKind::ALL.len()
        || !selection_is_canonical
        || !witness_shape_is_valid
    {
        return Err(RestoreRecoveryError::InvalidBinding);
    }
    Ok(())
}

fn validate_intent_id(intent_id: &str) -> Result<(), RestoreRecoveryError> {
    validate_id("restore physical intent id", intent_id)
        .map_err(|_| RestoreRecoveryError::InvalidIntentId)
}
