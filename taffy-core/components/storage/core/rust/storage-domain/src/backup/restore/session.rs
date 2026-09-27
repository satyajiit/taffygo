// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Consumptive authority phases for staged restore and candidate resolution.

use super::{BackupDigest, BackupError, RestorePlan};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RestoreSessionPhase {
    AwaitingRecoveryKey,
    Opening,
    AwaitingPlanConfirmation,
    Staging,
    VerifyingStaging,
    ReadyToCommit,
    CommitIssued,
    RollbackAvailable,
    AcceptIssued,
    RestorePreviousIssued,
    Complete,
    RolledBack,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RestoreSession {
    phase: RestoreSessionPhase,
    opened_snapshot_sha256: Option<[u8; 32]>,
    expected_staged_sha256: Option<[u8; 32]>,
    expected_confirmation_sha256: Option<[u8; 32]>,
}

impl RestoreSession {
    #[must_use]
    pub fn begin() -> Self {
        Self {
            phase: RestoreSessionPhase::AwaitingRecoveryKey,
            opened_snapshot_sha256: None,
            expected_staged_sha256: None,
            expected_confirmation_sha256: None,
        }
    }

    #[must_use]
    pub fn phase(&self) -> RestoreSessionPhase {
        self.phase
    }

    pub fn recovery_key_supplied(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::AwaitingRecoveryKey,
            RestoreSessionPhase::Opening,
        )
    }

    pub fn archive_opened(
        &mut self,
        plan: &RestorePlan,
        digest: &dyn BackupDigest,
    ) -> Result<[u8; 32], BackupError> {
        if self.phase != RestoreSessionPhase::Opening {
            return Err(BackupError::WrongPhase);
        }
        let confirmation_sha256 = plan.confirmation_digest(digest)?;
        self.opened_snapshot_sha256 = Some(plan.snapshot_sha256);
        self.expected_confirmation_sha256 = Some(confirmation_sha256);
        self.phase = RestoreSessionPhase::AwaitingPlanConfirmation;
        Ok(confirmation_sha256)
    }

    pub fn confirm_plan(
        &mut self,
        plan: &RestorePlan,
        confirmation_sha256: [u8; 32],
        digest: &dyn BackupDigest,
    ) -> Result<(), BackupError> {
        if self.phase != RestoreSessionPhase::AwaitingPlanConfirmation {
            return Err(BackupError::WrongPhase);
        }
        let recalculated = plan.confirmation_digest(digest)?;
        if plan.has_conflicts()
            || self.opened_snapshot_sha256 != Some(plan.snapshot_sha256)
            || self.expected_confirmation_sha256 != Some(recalculated)
        {
            return Err(BackupError::RestoreConflict);
        }
        if self.expected_confirmation_sha256 != Some(confirmation_sha256) {
            return Err(BackupError::RestoreNotConfirmed);
        }
        self.expected_staged_sha256 = Some(plan.snapshot_sha256);
        self.phase = RestoreSessionPhase::Staging;
        Ok(())
    }

    pub fn staging_finished(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::Staging,
            RestoreSessionPhase::VerifyingStaging,
        )
    }

    pub fn staging_verified(&mut self, staged_sha256: [u8; 32]) -> Result<(), BackupError> {
        if self.phase != RestoreSessionPhase::VerifyingStaging {
            return Err(BackupError::WrongPhase);
        }
        if self.expected_staged_sha256 != Some(staged_sha256) {
            self.phase = RestoreSessionPhase::RolledBack;
            return Err(BackupError::SnapshotMismatch);
        }
        self.phase = RestoreSessionPhase::ReadyToCommit;
        Ok(())
    }

    /// Consumes the only authority to begin the physical commit.
    pub fn issue_commit(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::ReadyToCommit,
            RestoreSessionPhase::CommitIssued,
        )
    }

    /// Records durable proof that the issued commit installed the candidate.
    pub fn commit_completed(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::CommitIssued,
            RestoreSessionPhase::RollbackAvailable,
        )
    }

    /// Records durable proof that the issued commit did not take effect.
    pub fn commit_definitely_not_applied(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::CommitIssued,
            RestoreSessionPhase::RolledBack,
        )
    }

    pub fn issue_accept_restored_profile(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::RollbackAvailable,
            RestoreSessionPhase::AcceptIssued,
        )
    }

    pub fn accept_restored_profile_completed(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::AcceptIssued,
            RestoreSessionPhase::Complete,
        )
    }

    pub fn accept_restored_profile_definitely_not_applied(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::AcceptIssued,
            RestoreSessionPhase::RollbackAvailable,
        )
    }

    pub fn issue_restore_previous_generation(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::RollbackAvailable,
            RestoreSessionPhase::RestorePreviousIssued,
        )
    }

    pub fn restore_previous_generation_completed(&mut self) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::RestorePreviousIssued,
            RestoreSessionPhase::RolledBack,
        )
    }

    pub fn restore_previous_generation_definitely_not_applied(
        &mut self,
    ) -> Result<(), BackupError> {
        self.advance(
            RestoreSessionPhase::RestorePreviousIssued,
            RestoreSessionPhase::RollbackAvailable,
        )
    }

    /// Cancels only while no physical commit has been issued.
    pub fn cancel_before_commit(&mut self) -> Result<(), BackupError> {
        if matches!(
            self.phase,
            RestoreSessionPhase::AwaitingRecoveryKey
                | RestoreSessionPhase::Opening
                | RestoreSessionPhase::AwaitingPlanConfirmation
                | RestoreSessionPhase::Staging
                | RestoreSessionPhase::VerifyingStaging
                | RestoreSessionPhase::ReadyToCommit
        ) {
            self.phase = RestoreSessionPhase::RolledBack;
            Ok(())
        } else {
            Err(BackupError::WrongPhase)
        }
    }

    #[must_use]
    pub const fn outcome_requires_reconciliation(&self) -> bool {
        matches!(
            self.phase,
            RestoreSessionPhase::CommitIssued
                | RestoreSessionPhase::AcceptIssued
                | RestoreSessionPhase::RestorePreviousIssued
                | RestoreSessionPhase::RollbackAvailable
        )
    }

    fn advance(
        &mut self,
        expected: RestoreSessionPhase,
        next: RestoreSessionPhase,
    ) -> Result<(), BackupError> {
        if self.phase != expected {
            return Err(BackupError::WrongPhase);
        }
        self.phase = next;
        Ok(())
    }
}

impl Default for RestoreSession {
    fn default() -> Self {
        Self::begin()
    }
}
