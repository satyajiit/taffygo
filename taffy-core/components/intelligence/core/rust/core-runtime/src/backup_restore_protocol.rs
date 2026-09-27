// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Source-profile authority for one explicit restore.
//!
//! This protocol retains the validated portable plan. It carries no recovery
//! key, archive stream, file path, or physical-profile handle. Selected staged
//! procedure definitions are validated transiently before commit authority;
//! they never replace the source catalogue. The browser may act only on a
//! consumptive authorization returned from the exact phase.

use taffy_storage::backup::{BackupError, RestorePlan, RestoreSession, RestoreSessionPhase};

use crate::backup_planning::{
    failed_restore, prepare_restore, valid_operation, BackupDigestPort, PreparedRestore,
};
use crate::{wire, Sha256Port};

mod lifecycle;
mod procedure_admission;
mod recovery;
mod recovery_resolution;
mod types;

use recovery_resolution::{RecoveryResolutionIssue, RecoveryResolutionReceipt};

pub use types::{
    BackupRestoreBinding, BackupRestoreCommitAuthorization, BackupRestoreCommitOutcome,
    BackupRestoreProtocolError, BackupRestoreResolutionAuthorization,
    BackupRestoreResolutionChoice, BackupRestoreResolutionOutcome, BackupRestoreStageAuthorization,
};

#[derive(Debug)]
struct ActiveRestore {
    binding: BackupRestoreBinding,
    plan: RestorePlan,
    session: RestoreSession,
    stage_authorization: Option<BackupRestoreStageAuthorization>,
    commit_authorization: Option<BackupRestoreCommitAuthorization>,
    resolution_authorization: Option<BackupRestoreResolutionAuthorization>,
}

/// One source-Core-owned restore protocol for a profile generation.
#[derive(Debug)]
pub struct BackupRestoreProtocol {
    owner_profile_id: String,
    generation: u64,
    private_profile: bool,
    connected: bool,
    active: Option<ActiveRestore>,
    // One content-free terminal receipt for a lost cancellation reply. It is
    // incarnation-local and cannot authorize staging, commit or resolution.
    last_cancelled_binding: Option<BackupRestoreBinding>,
    // Incarnation-local custody for one recovery resolution decision. Durable
    // history remains the only post-restart source of physical truth.
    recovery_resolution: Option<RecoveryResolutionIssue>,
    // Bounded exact receipt for a lost definitive report reply. It settles no
    // different history and carries no permission to repeat physical work.
    recovery_resolution_receipt: Option<RecoveryResolutionReceipt>,
}

impl BackupRestoreProtocol {
    pub(crate) fn new(owner_profile_id: String, generation: u64, private_profile: bool) -> Self {
        Self {
            owner_profile_id,
            generation,
            private_profile,
            connected: true,
            active: None,
            last_cancelled_binding: None,
            recovery_resolution: None,
            recovery_resolution_receipt: None,
        }
    }

    /// Plans against a browser-reserved dormant target, which must be distinct
    /// from the source profile that owns this protocol.
    pub fn plan_restore(
        &mut self,
        request: wire::BackupRestorePlanRequest,
        now_monotonic_ms: u64,
        digest: &dyn Sha256Port,
    ) -> wire::BackupRestorePlanResult {
        let operation = request.operation.clone();
        let target_kind = request.target.kind;
        if !self.connected {
            return failed_restore(
                operation,
                target_kind,
                wire::BackupPlanningStatus::Unavailable,
            );
        }
        if self.private_profile || self.active.is_some() || self.recovery_resolution.is_some() {
            return failed_restore(
                operation,
                target_kind,
                wire::BackupPlanningStatus::Unavailable,
            );
        }
        if target_kind != wire::BackupRestoreTargetKind::NewRegularProfile
            || request.target.profile_id == self.owner_profile_id
        {
            return failed_restore(
                operation,
                target_kind,
                wire::BackupPlanningStatus::InvalidRequest,
            );
        }
        let prepared = match prepare_restore(request, self.generation, now_monotonic_ms, digest) {
            Ok(prepared) => prepared,
            Err(failure) => return *failure,
        };
        let mut result = prepared.to_wire();
        let active = self.activate(prepared);
        // A replay of the exact cancelled planning request is a terminal
        // identity, not a new restore. Otherwise a delayed exact cancellation
        // could act on a freshly reactivated session with that same binding.
        if self.last_cancelled_binding.as_ref() == Some(&active.binding) {
            return failed_restore(
                operation,
                target_kind,
                wire::BackupPlanningStatus::InvalidRequest,
            );
        }
        result.binding = Some(active.binding.to_wire());
        self.active = Some(active);
        result
    }

    pub fn confirm_plan(
        &mut self,
        decision_operation: wire::OperationEnvelope,
        binding: &BackupRestoreBinding,
        confirmed_sha256: [u8; 32],
        now_monotonic_ms: u64,
        digest: &dyn Sha256Port,
    ) -> Result<BackupRestoreStageAuthorization, BackupRestoreProtocolError> {
        self.validate_decision(&decision_operation, now_monotonic_ms)?;
        let active = self.active_mut(binding)?;
        active
            .session
            .confirm_plan(&active.plan, confirmed_sha256, &BackupDigestPort(digest))
            .map_err(|error| map_domain_error(&error))?;
        let authorization = BackupRestoreStageAuthorization {
            binding: binding.clone(),
            decision_operation,
        };
        active.stage_authorization = Some(authorization.clone());
        Ok(authorization)
    }

    pub fn report_stage_verified(
        &mut self,
        decision_operation: wire::OperationEnvelope,
        authorization: &BackupRestoreStageAuthorization,
        staged_snapshot_sha256: [u8; 32],
        skills: Vec<wire::SkillRecord>,
        now_monotonic_ms: u64,
    ) -> Result<BackupRestoreCommitAuthorization, BackupRestoreProtocolError> {
        self.validate_decision(&decision_operation, now_monotonic_ms)?;
        let active = self.active_mut(&authorization.binding)?;
        if active.stage_authorization.as_ref() != Some(authorization) {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        if active.session.phase() != RestoreSessionPhase::Staging {
            return Err(BackupRestoreProtocolError::WrongPhase);
        }
        if let Err(error) =
            procedure_admission::validate_plan_procedures(active.plan.entries(), skills)
        {
            self.active = None;
            return Err(error);
        }
        active
            .session
            .staging_finished()
            .map_err(|error| map_domain_error(&error))?;
        if let Err(error) = active.session.staging_verified(staged_snapshot_sha256) {
            let mapped = map_domain_error(&error);
            self.active = None;
            return Err(mapped);
        }
        active
            .session
            .issue_commit()
            .map_err(|error| map_domain_error(&error))?;
        let commit = BackupRestoreCommitAuthorization {
            binding: authorization.binding.clone(),
            decision_operation,
        };
        active.commit_authorization = Some(commit.clone());
        Ok(commit)
    }

    pub fn resolve_commit(
        &mut self,
        report_operation: &wire::OperationEnvelope,
        authorization: &BackupRestoreCommitAuthorization,
        outcome: BackupRestoreCommitOutcome,
        now_monotonic_ms: u64,
    ) -> Result<(), BackupRestoreProtocolError> {
        self.validate_decision(report_operation, now_monotonic_ms)?;
        let active = self.active_mut(&authorization.binding)?;
        if active.commit_authorization.as_ref() != Some(authorization) {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        if active.session.phase() != RestoreSessionPhase::CommitIssued {
            return Err(BackupRestoreProtocolError::WrongPhase);
        }
        match outcome {
            BackupRestoreCommitOutcome::OutcomeUnknown => {
                Err(BackupRestoreProtocolError::ReconcileRequired)
            }
            BackupRestoreCommitOutcome::Committed => active
                .session
                .commit_completed()
                .map_err(|error| map_domain_error(&error)),
            BackupRestoreCommitOutcome::DefinitelyNotCommitted => {
                active
                    .session
                    .commit_definitely_not_applied()
                    .map_err(|error| map_domain_error(&error))?;
                self.active = None;
                Ok(())
            }
        }
    }

    pub fn choose_resolution(
        &mut self,
        decision_operation: wire::OperationEnvelope,
        binding: &BackupRestoreBinding,
        choice: BackupRestoreResolutionChoice,
        now_monotonic_ms: u64,
    ) -> Result<BackupRestoreResolutionAuthorization, BackupRestoreProtocolError> {
        self.validate_decision(&decision_operation, now_monotonic_ms)?;
        let active = self.active_mut(binding)?;
        match choice {
            BackupRestoreResolutionChoice::AcceptCandidate => active
                .session
                .issue_accept_restored_profile()
                .map_err(|error| map_domain_error(&error))?,
            BackupRestoreResolutionChoice::DiscardCandidate => active
                .session
                .issue_restore_previous_generation()
                .map_err(|error| map_domain_error(&error))?,
        }
        let authorization = BackupRestoreResolutionAuthorization {
            binding: binding.clone(),
            decision_operation,
            choice,
        };
        active.resolution_authorization = Some(authorization.clone());
        Ok(authorization)
    }

    pub fn resolve_resolution(
        &mut self,
        report_operation: &wire::OperationEnvelope,
        authorization: &BackupRestoreResolutionAuthorization,
        outcome: BackupRestoreResolutionOutcome,
        now_monotonic_ms: u64,
    ) -> Result<(), BackupRestoreProtocolError> {
        self.validate_decision(report_operation, now_monotonic_ms)?;
        let active = self.active_mut(&authorization.binding)?;
        if active.resolution_authorization.as_ref() != Some(authorization) {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        let expected_phase = match authorization.choice {
            BackupRestoreResolutionChoice::AcceptCandidate => RestoreSessionPhase::AcceptIssued,
            BackupRestoreResolutionChoice::DiscardCandidate => {
                RestoreSessionPhase::RestorePreviousIssued
            }
        };
        if active.session.phase() != expected_phase {
            return Err(BackupRestoreProtocolError::WrongPhase);
        }
        if outcome == BackupRestoreResolutionOutcome::OutcomeUnknown {
            return Err(BackupRestoreProtocolError::ReconcileRequired);
        }
        let completed = outcome == BackupRestoreResolutionOutcome::Completed;
        match (authorization.choice, completed) {
            (BackupRestoreResolutionChoice::AcceptCandidate, true) => active
                .session
                .accept_restored_profile_completed()
                .map_err(|error| map_domain_error(&error))?,
            (BackupRestoreResolutionChoice::AcceptCandidate, false) => active
                .session
                .accept_restored_profile_definitely_not_applied()
                .map_err(|error| map_domain_error(&error))?,
            (BackupRestoreResolutionChoice::DiscardCandidate, true) => active
                .session
                .restore_previous_generation_completed()
                .map_err(|error| map_domain_error(&error))?,
            (BackupRestoreResolutionChoice::DiscardCandidate, false) => active
                .session
                .restore_previous_generation_definitely_not_applied()
                .map_err(|error| map_domain_error(&error))?,
        }
        if completed {
            self.active = None;
        } else if let Some(active) = self.active.as_mut() {
            active.resolution_authorization = None;
        }
        Ok(())
    }

    pub fn cancel_before_commit(
        &mut self,
        decision_operation: &wire::OperationEnvelope,
        binding: &BackupRestoreBinding,
        now_monotonic_ms: u64,
    ) -> Result<(), BackupRestoreProtocolError> {
        self.validate_decision(decision_operation, now_monotonic_ms)?;
        self.cancel_exact(binding)
    }

    fn activate(&self, prepared: PreparedRestore) -> ActiveRestore {
        let binding = BackupRestoreBinding {
            planning_operation: prepared.operation,
            owner_profile_id: self.owner_profile_id.clone(),
            target_kind: wire::BackupRestoreTargetKind::NewRegularProfile,
            target_profile_id: prepared.plan.target().profile_id.clone(),
            backup_id: prepared.plan.backup_id().to_owned(),
            snapshot_sha256: prepared.plan.snapshot_sha256(),
            confirmation_sha256: prepared.confirmation_sha256,
        };
        ActiveRestore {
            binding,
            plan: prepared.plan,
            session: prepared.session,
            stage_authorization: None,
            commit_authorization: None,
            resolution_authorization: None,
        }
    }

    fn validate_decision(
        &self,
        operation: &wire::OperationEnvelope,
        now_monotonic_ms: u64,
    ) -> Result<(), BackupRestoreProtocolError> {
        if !self.connected {
            return Err(BackupRestoreProtocolError::Unavailable);
        }
        valid_operation(operation, self.generation, now_monotonic_ms)
            .then_some(())
            .ok_or(BackupRestoreProtocolError::InvalidOperation)
    }

    fn active_mut(
        &mut self,
        binding: &BackupRestoreBinding,
    ) -> Result<&mut ActiveRestore, BackupRestoreProtocolError> {
        let active = self
            .active
            .as_mut()
            .ok_or(BackupRestoreProtocolError::Unavailable)?;
        if active.binding != *binding {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
        Ok(active)
    }

    fn cancel_exact(
        &mut self,
        binding: &BackupRestoreBinding,
    ) -> Result<(), BackupRestoreProtocolError> {
        // A fresh valid decision can observe the previous exact cancellation
        // even after its active session was removed. Never let that receipt
        // hide a new active session with the same binding or affect another
        // restore. Keeping one receipt bounds memory for this incarnation.
        if self.last_cancelled_binding.as_ref() == Some(binding)
            && self
                .active
                .as_ref()
                .is_none_or(|active| active.binding != *binding)
        {
            return Ok(());
        }
        let active = self.active_mut(binding)?;
        active.session.cancel_before_commit().map_err(|_| {
            if active.session.outcome_requires_reconciliation() {
                BackupRestoreProtocolError::ReconcileRequired
            } else {
                BackupRestoreProtocolError::WrongPhase
            }
        })?;
        self.last_cancelled_binding = Some(binding.clone());
        self.active = None;
        Ok(())
    }
}

const fn map_domain_error(error: &BackupError) -> BackupRestoreProtocolError {
    match error {
        BackupError::WrongPhase => BackupRestoreProtocolError::WrongPhase,
        BackupError::RestoreNotConfirmed => BackupRestoreProtocolError::ConfirmationMismatch,
        BackupError::SnapshotMismatch => BackupRestoreProtocolError::SnapshotMismatch,
        _ => BackupRestoreProtocolError::Unavailable,
    }
}

#[cfg(test)]
mod tests;
