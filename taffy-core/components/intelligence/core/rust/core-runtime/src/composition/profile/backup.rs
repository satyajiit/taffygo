// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Profile-owned projection of portable restore authority.

use std::rc::Rc;

use taffy_storage::backup::RestoreSessionPhase;

use crate::backup_restore_protocol::{
    BackupRestoreBinding, BackupRestoreCommitAuthorization, BackupRestoreCommitOutcome,
    BackupRestoreProtocolError, BackupRestoreResolutionAuthorization,
    BackupRestoreResolutionChoice, BackupRestoreResolutionOutcome, BackupRestoreStageAuthorization,
};
use crate::wire;

use super::ProfileServiceRuntime;

impl ProfileServiceRuntime {
    pub fn choose_backup_restore_recovery_resolution(
        &mut self,
        request: wire::BackupRestoreRecoveryResolutionRequest,
        now_monotonic_ms: u64,
    ) -> wire::BackupRestoreRecoveryResolutionAuthorizationResult {
        self.backup_restore
            .choose_recovery_resolution(request, now_monotonic_ms)
    }

    pub fn report_backup_restore_recovery_resolution_outcome(
        &mut self,
        report: wire::BackupRestoreRecoveryResolutionOutcomeReport,
        now_monotonic_ms: u64,
    ) -> wire::BackupRestoreProtocolResult {
        self.backup_restore
            .report_recovery_resolution_outcome(report, now_monotonic_ms)
    }

    pub fn inspect_backup_restore_recovery(
        &self,
        request: wire::BackupRestoreRecoveryInspectionRequest,
        now_monotonic_ms: u64,
    ) -> wire::BackupRestoreRecoveryInspectionResult {
        self.backup_restore
            .inspect_recovery(request, now_monotonic_ms)
    }

    pub fn plan_backup_restore(
        &mut self,
        request: wire::BackupRestorePlanRequest,
        now_monotonic_ms: u64,
    ) -> wire::BackupRestorePlanResult {
        let digest = Rc::clone(&self.digest);
        self.backup_restore
            .plan_restore(request, now_monotonic_ms, digest.as_ref())
    }

    pub fn confirm_backup_restore_plan(
        &mut self,
        operation: wire::OperationEnvelope,
        binding: &BackupRestoreBinding,
        confirmed_sha256: [u8; 32],
        now_monotonic_ms: u64,
    ) -> Result<BackupRestoreStageAuthorization, BackupRestoreProtocolError> {
        let digest = Rc::clone(&self.digest);
        self.backup_restore.confirm_plan(
            operation,
            binding,
            confirmed_sha256,
            now_monotonic_ms,
            digest.as_ref(),
        )
    }

    pub fn report_backup_restore_stage_verified(
        &mut self,
        operation: wire::OperationEnvelope,
        authorization: &BackupRestoreStageAuthorization,
        staged_snapshot_sha256: [u8; 32],
        skills: Vec<wire::SkillRecord>,
        now_monotonic_ms: u64,
    ) -> Result<BackupRestoreCommitAuthorization, BackupRestoreProtocolError> {
        self.backup_restore.report_stage_verified(
            operation,
            authorization,
            staged_snapshot_sha256,
            skills,
            now_monotonic_ms,
        )
    }

    pub fn resolve_backup_restore_commit(
        &mut self,
        operation: &wire::OperationEnvelope,
        authorization: &BackupRestoreCommitAuthorization,
        outcome: BackupRestoreCommitOutcome,
        now_monotonic_ms: u64,
    ) -> Result<(), BackupRestoreProtocolError> {
        self.backup_restore
            .resolve_commit(operation, authorization, outcome, now_monotonic_ms)
    }

    pub fn choose_backup_restore_resolution(
        &mut self,
        operation: wire::OperationEnvelope,
        binding: &BackupRestoreBinding,
        choice: BackupRestoreResolutionChoice,
        now_monotonic_ms: u64,
    ) -> Result<BackupRestoreResolutionAuthorization, BackupRestoreProtocolError> {
        self.backup_restore
            .choose_resolution(operation, binding, choice, now_monotonic_ms)
    }

    pub fn resolve_backup_restore_resolution(
        &mut self,
        operation: &wire::OperationEnvelope,
        authorization: &BackupRestoreResolutionAuthorization,
        outcome: BackupRestoreResolutionOutcome,
        now_monotonic_ms: u64,
    ) -> Result<(), BackupRestoreProtocolError> {
        self.backup_restore
            .resolve_resolution(operation, authorization, outcome, now_monotonic_ms)
    }

    pub fn cancel_backup_restore_before_commit(
        &mut self,
        operation: &wire::OperationEnvelope,
        binding: &BackupRestoreBinding,
        now_monotonic_ms: u64,
    ) -> Result<(), BackupRestoreProtocolError> {
        self.backup_restore
            .cancel_before_commit(operation, binding, now_monotonic_ms)
    }

    pub fn disconnect_backup_restore(&mut self) -> Result<(), BackupRestoreProtocolError> {
        self.backup_restore.service_disconnected()
    }

    pub fn active_backup_restore_binding(&self) -> Option<&BackupRestoreBinding> {
        self.backup_restore.active_binding()
    }

    pub fn backup_restore_phase(&self) -> Option<RestoreSessionPhase> {
        self.backup_restore.phase()
    }
}
