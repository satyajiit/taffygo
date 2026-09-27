// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Value-only proofs projected to the browser by a later generated seam.

use crate::wire;

/// Immutable identity of the retained plan and browser-reserved target.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreBinding {
    pub(super) planning_operation: wire::OperationEnvelope,
    pub(super) owner_profile_id: String,
    pub(super) target_kind: wire::BackupRestoreTargetKind,
    pub(super) target_profile_id: String,
    pub(super) backup_id: String,
    pub(super) snapshot_sha256: [u8; 32],
    pub(super) confirmation_sha256: [u8; 32],
}

impl BackupRestoreBinding {
    #[must_use]
    pub fn from_wire(input: wire::BackupRestoreBinding) -> Self {
        Self {
            planning_operation: input.planning_operation,
            owner_profile_id: input.owner_profile_id,
            target_kind: input.target.kind,
            target_profile_id: input.target.profile_id,
            backup_id: input.backup_id,
            snapshot_sha256: input.snapshot_sha256,
            confirmation_sha256: input.confirmation_sha256,
        }
    }

    #[must_use]
    pub fn to_wire(&self) -> wire::BackupRestoreBinding {
        wire::BackupRestoreBinding {
            planning_operation: self.planning_operation.clone(),
            owner_profile_id: self.owner_profile_id.clone(),
            target: wire::BackupRestoreTarget {
                kind: self.target_kind,
                profile_id: self.target_profile_id.clone(),
            },
            backup_id: self.backup_id.clone(),
            snapshot_sha256: self.snapshot_sha256,
            confirmation_sha256: self.confirmation_sha256,
        }
    }

    pub fn planning_operation(&self) -> &wire::OperationEnvelope {
        &self.planning_operation
    }

    pub fn owner_profile_id(&self) -> &str {
        &self.owner_profile_id
    }

    pub const fn target_kind(&self) -> wire::BackupRestoreTargetKind {
        self.target_kind
    }

    pub fn target_profile_id(&self) -> &str {
        &self.target_profile_id
    }

    pub fn backup_id(&self) -> &str {
        &self.backup_id
    }

    pub const fn snapshot_sha256(&self) -> [u8; 32] {
        self.snapshot_sha256
    }

    pub const fn confirmation_sha256(&self) -> [u8; 32] {
        self.confirmation_sha256
    }
}

macro_rules! authorization {
    ($name:ident) => {
        #[derive(Clone, Debug, Eq, PartialEq)]
        pub struct $name {
            pub(super) binding: BackupRestoreBinding,
            pub(super) decision_operation: wire::OperationEnvelope,
        }

        impl $name {
            #[must_use]
            pub fn from_parts(
                binding: BackupRestoreBinding,
                decision_operation: wire::OperationEnvelope,
            ) -> Self {
                Self {
                    binding,
                    decision_operation,
                }
            }

            pub fn binding(&self) -> &BackupRestoreBinding {
                &self.binding
            }

            pub fn decision_operation(&self) -> &wire::OperationEnvelope {
                &self.decision_operation
            }
        }
    };
}

authorization!(BackupRestoreStageAuthorization);
authorization!(BackupRestoreCommitAuthorization);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BackupRestoreResolutionChoice {
    AcceptCandidate,
    DiscardCandidate,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreResolutionAuthorization {
    pub(super) binding: BackupRestoreBinding,
    pub(super) decision_operation: wire::OperationEnvelope,
    pub(super) choice: BackupRestoreResolutionChoice,
}

impl BackupRestoreResolutionAuthorization {
    #[must_use]
    pub fn from_parts(
        binding: BackupRestoreBinding,
        decision_operation: wire::OperationEnvelope,
        choice: BackupRestoreResolutionChoice,
    ) -> Self {
        Self {
            binding,
            decision_operation,
            choice,
        }
    }

    pub fn binding(&self) -> &BackupRestoreBinding {
        &self.binding
    }

    pub fn decision_operation(&self) -> &wire::OperationEnvelope {
        &self.decision_operation
    }

    pub const fn choice(&self) -> BackupRestoreResolutionChoice {
        self.choice
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BackupRestoreCommitOutcome {
    Committed,
    DefinitelyNotCommitted,
    OutcomeUnknown,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BackupRestoreResolutionOutcome {
    Completed,
    DefinitelyNotCompleted,
    OutcomeUnknown,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BackupRestoreProtocolError {
    Unavailable,
    InvalidOperation,
    BindingMismatch,
    WrongPhase,
    ConfirmationMismatch,
    SnapshotMismatch,
    ReconcileRequired,
}
