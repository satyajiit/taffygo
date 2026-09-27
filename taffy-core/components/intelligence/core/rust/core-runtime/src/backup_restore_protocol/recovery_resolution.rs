// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Consumptive resolution authority over one exact durable recovery prefix.

use taffy_storage::backup::{
    validate_restore_recovery_history, RestoreRecoveryFact, RestoreRecoveryRecord,
    RestoreRecoveryStatus, RestoreSessionPhase, MAX_BACKUP_RESTORE_RECOVERY_RECORDS,
    RESTORE_RECOVERY_RECORD_VERSION,
};

use super::recovery::records_from_wire;
use super::{BackupRestoreProtocol, BackupRestoreProtocolError};
use crate::wire;

mod validation;

use validation::{
    admit_resolution_choice, authorization_result, intent_for_choice, is_expected_settlement,
    recovery_binding_matches_active, status_for, validate_report_suffix,
};

const RECOVERY_RESOLUTION_SUFFIX_CAPACITY: usize = 3;
const MAX_RECOVERY_RESOLUTION_PREFIX_RECORDS: usize =
    MAX_BACKUP_RESTORE_RECOVERY_RECORDS - RECOVERY_RESOLUTION_SUFFIX_CAPACITY;

#[derive(Debug)]
pub(super) struct RecoveryResolutionIssue {
    authorization: wire::BackupRestoreRecoveryResolutionAuthorization,
    prior_status: RestoreRecoveryStatus,
    advances_live_session: bool,
    observed_history: Option<Vec<wire::BackupRestoreRecoveryRecord>>,
}

#[derive(Debug)]
pub(super) struct RecoveryResolutionReceipt {
    authorization: wire::BackupRestoreRecoveryResolutionAuthorization,
    durable_history: Vec<wire::BackupRestoreRecoveryRecord>,
}

impl BackupRestoreProtocol {
    /// Issues one exact accept-or-discard decision after reducing the complete
    /// durable prefix. Repeating the same logical request may replace only an
    /// expired decision operation; it never creates another physical intent.
    pub fn choose_recovery_resolution(
        &mut self,
        request: wire::BackupRestoreRecoveryResolutionRequest,
        now_monotonic_ms: u64,
    ) -> wire::BackupRestoreRecoveryResolutionAuthorizationResult {
        let operation = request.operation.clone();
        match self.issue_recovery_resolution(request, now_monotonic_ms) {
            Ok(authorization) => authorization_result(
                operation,
                wire::BackupRestoreProtocolStatus::Succeeded,
                Some(authorization),
            ),
            Err(error) => authorization_result(operation, status_for(error), None),
        }
    }

    /// Reduces the exact durable suffix for a previously issued intent. No
    /// caller-supplied outcome can settle the portable phase.
    pub fn report_recovery_resolution_outcome(
        &mut self,
        report: wire::BackupRestoreRecoveryResolutionOutcomeReport,
        now_monotonic_ms: u64,
    ) -> wire::BackupRestoreProtocolResult {
        let operation = report.operation.clone();
        let status = match self.apply_recovery_resolution_report(report, now_monotonic_ms) {
            Ok(()) => wire::BackupRestoreProtocolStatus::Succeeded,
            Err(error) => status_for(error),
        };
        wire::BackupRestoreProtocolResult { operation, status }
    }

    fn issue_recovery_resolution(
        &mut self,
        request: wire::BackupRestoreRecoveryResolutionRequest,
        now_monotonic_ms: u64,
    ) -> Result<wire::BackupRestoreRecoveryResolutionAuthorization, BackupRestoreProtocolError>
    {
        self.validate_decision(&request.operation, now_monotonic_ms)?;
        if self.private_profile {
            return Err(BackupRestoreProtocolError::Unavailable);
        }

        if let Some(issue) = self.recovery_resolution.as_mut() {
            let authorization = &mut issue.authorization;
            if authorization.history_prefix != request.history_prefix
                || authorization.choice != request.choice
                || authorization.intent_id != request.intent_id
            {
                return Err(BackupRestoreProtocolError::BindingMismatch);
            }
            if issue.observed_history.is_some() {
                return Err(BackupRestoreProtocolError::ReconcileRequired);
            }
            if authorization.decision_operation == request.operation {
                return Ok(authorization.clone());
            }
            if authorization.decision_operation.deadline_monotonic_ms > now_monotonic_ms {
                return Err(BackupRestoreProtocolError::WrongPhase);
            }
            authorization.decision_operation = request.operation;
            return Ok(authorization.clone());
        }

        if self
            .recovery_resolution_receipt
            .as_ref()
            .is_some_and(|receipt| receipt.authorization.history_prefix == request.history_prefix)
        {
            return Err(BackupRestoreProtocolError::WrongPhase);
        }

        if request.history_prefix.len() > MAX_RECOVERY_RESOLUTION_PREFIX_RECORDS {
            return Err(BackupRestoreProtocolError::WrongPhase);
        }
        if request.history_prefix.iter().any(|record| {
            record.binding.owner_profile_id != self.owner_profile_id || !record.has_valid_body()
        }) {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        let mut domain_records = records_from_wire(&request.history_prefix)
            .ok_or(BackupRestoreProtocolError::BindingMismatch)?;
        let prior_status = validate_restore_recovery_history(&domain_records)
            .map_err(|_| BackupRestoreProtocolError::BindingMismatch)?;
        let intent = intent_for_choice(request.choice);
        admit_resolution_choice(&prior_status, request.choice)?;

        let binding = domain_records
            .first()
            .map(|record| record.binding.clone())
            .ok_or(BackupRestoreProtocolError::BindingMismatch)?;
        let next_sequence = u64::try_from(domain_records.len() + 1)
            .map_err(|_| BackupRestoreProtocolError::BindingMismatch)?;
        domain_records.push(RestoreRecoveryRecord {
            format_version: RESTORE_RECOVERY_RECORD_VERSION,
            sequence: next_sequence,
            binding,
            fact: RestoreRecoveryFact::IntentRecorded {
                intent_id: request.intent_id.clone(),
                intent,
            },
        });
        if validate_restore_recovery_history(&domain_records)
            != Ok(RestoreRecoveryStatus::ReconcileRequired {
                intent_id: request.intent_id.clone(),
                intent,
            })
        {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }

        let recovery_binding = request
            .history_prefix
            .first()
            .map(|record| record.binding.clone())
            .ok_or(BackupRestoreProtocolError::BindingMismatch)?;
        let advances_live_session =
            self.advance_matching_live_session(&recovery_binding, request.choice, &prior_status)?;
        let authorization = wire::BackupRestoreRecoveryResolutionAuthorization {
            binding: recovery_binding,
            decision_operation: request.operation,
            choice: request.choice,
            intent_id: request.intent_id,
            history_prefix: request.history_prefix,
        };
        self.recovery_resolution = Some(RecoveryResolutionIssue {
            authorization: authorization.clone(),
            prior_status,
            advances_live_session,
            observed_history: None,
        });
        Ok(authorization)
    }

    fn apply_recovery_resolution_report(
        &mut self,
        report: wire::BackupRestoreRecoveryResolutionOutcomeReport,
        now_monotonic_ms: u64,
    ) -> Result<(), BackupRestoreProtocolError> {
        self.validate_decision(&report.operation, now_monotonic_ms)?;
        if self.recovery_resolution.is_none()
            && self
                .recovery_resolution_receipt
                .as_ref()
                .is_some_and(|receipt| {
                    receipt.authorization == report.authorization
                        && receipt.durable_history == report.durable_history
                })
        {
            return Ok(());
        }
        let issue = self
            .recovery_resolution
            .as_ref()
            .ok_or(BackupRestoreProtocolError::Unavailable)?;
        if report.authorization != issue.authorization {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        validate_report_suffix(&issue.authorization, &report.durable_history)?;
        if issue.observed_history.as_ref().is_some_and(|observed| {
            report.durable_history.len() < observed.len()
                || report.durable_history.get(..observed.len()) != Some(observed.as_slice())
        }) {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        if report.durable_history.iter().any(|record| {
            record.binding.owner_profile_id != self.owner_profile_id || !record.has_valid_body()
        }) {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        let records = records_from_wire(&report.durable_history)
            .ok_or(BackupRestoreProtocolError::BindingMismatch)?;
        let status = validate_restore_recovery_history(&records)
            .map_err(|_| BackupRestoreProtocolError::BindingMismatch)?;
        if matches!(&status, RestoreRecoveryStatus::ReconcileRequired { .. }) {
            // `Unavailable` is unreachable here — the `ok_or` above already
            // proved the issue is present — but it is the same refusal that
            // check gives, so a future edit that breaks the invariant refuses
            // instead of ending the profile's generation with a panic.
            let issue = self
                .recovery_resolution
                .as_mut()
                .ok_or(BackupRestoreProtocolError::Unavailable)?;
            issue.observed_history = Some(report.durable_history);
            return Err(BackupRestoreProtocolError::ReconcileRequired);
        }
        if !is_expected_settlement(issue, &status) {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }

        let issue = self
            .recovery_resolution
            .take()
            .ok_or(BackupRestoreProtocolError::Unavailable)?;
        if issue.advances_live_session {
            self.settle_live_session(issue.authorization.choice, &status)?;
        }
        self.recovery_resolution_receipt = Some(RecoveryResolutionReceipt {
            authorization: issue.authorization,
            durable_history: report.durable_history,
        });
        Ok(())
    }

    fn advance_matching_live_session(
        &mut self,
        binding: &wire::BackupRestoreRecoveryBinding,
        choice: wire::BackupRestoreResolutionChoice,
        prior_status: &RestoreRecoveryStatus,
    ) -> Result<bool, BackupRestoreProtocolError> {
        let Some(active) = self.active.as_mut() else {
            return Ok(false);
        };
        if !recovery_binding_matches_active(binding, &active.binding) {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        if active.session.phase() == RestoreSessionPhase::CommitIssued {
            match prior_status {
                RestoreRecoveryStatus::RollbackAvailable => active
                    .session
                    .commit_completed()
                    .map_err(|_| BackupRestoreProtocolError::WrongPhase)?,
                RestoreRecoveryStatus::CleanupRequired => {
                    active
                        .session
                        .commit_definitely_not_applied()
                        .map_err(|_| BackupRestoreProtocolError::WrongPhase)?;
                    // The durable fact proves that the candidate was never
                    // installed. Retire this in-memory session before issuing
                    // recovery-only cleanup authority below.
                    self.active = None;
                    return Ok(false);
                }
                _ => return Err(BackupRestoreProtocolError::WrongPhase),
            }
        }
        match choice {
            wire::BackupRestoreResolutionChoice::AcceptCandidate => active
                .session
                .issue_accept_restored_profile()
                .map_err(|_| BackupRestoreProtocolError::WrongPhase)?,
            wire::BackupRestoreResolutionChoice::DiscardCandidate => active
                .session
                .issue_restore_previous_generation()
                .map_err(|_| BackupRestoreProtocolError::WrongPhase)?,
        }
        Ok(true)
    }

    fn settle_live_session(
        &mut self,
        choice: wire::BackupRestoreResolutionChoice,
        status: &RestoreRecoveryStatus,
    ) -> Result<(), BackupRestoreProtocolError> {
        let active = self
            .active
            .as_mut()
            .ok_or(BackupRestoreProtocolError::Unavailable)?;
        let completed = matches!(
            status,
            RestoreRecoveryStatus::Published | RestoreRecoveryStatus::VerifiedDeleted
        );
        match (choice, completed) {
            (wire::BackupRestoreResolutionChoice::AcceptCandidate, true) => {
                active.session.accept_restored_profile_completed()
            }
            (wire::BackupRestoreResolutionChoice::AcceptCandidate, false) => active
                .session
                .accept_restored_profile_definitely_not_applied(),
            (wire::BackupRestoreResolutionChoice::DiscardCandidate, true) => {
                active.session.restore_previous_generation_completed()
            }
            (wire::BackupRestoreResolutionChoice::DiscardCandidate, false) => active
                .session
                .restore_previous_generation_definitely_not_applied(),
        }
        .map_err(|_| BackupRestoreProtocolError::WrongPhase)?;
        if completed {
            self.active = None;
        }
        Ok(())
    }
}
