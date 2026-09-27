// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::CommandKind;
use crate::tool::IdempotencyClass;

impl CommandKind {
    /// The idempotency class of the command itself (section 18.2).
    ///
    /// Retrying any command through the reducer is safe, because a duplicate
    /// key returns the original result and performs no effect. The class is
    /// here so an audit record can say what a repeat would have meant if it had
    /// reached the world instead of the reducer.
    /// Exhaustive on purpose. A catch-all arm would give a command added later
    /// the mildest class in the taxonomy without anybody deciding that, and the
    /// one thing this function exists to record is a decision. A new member is
    /// a compile error here, which is the cheapest possible moment to make it.
    pub const fn idempotency(self) -> IdempotencyClass {
        match self {
            // Both leave the process and neither can be taken back. A
            // dispatch may reach the page; a model turn reaches a provider and
            // is paid for, so a second delivery that got past the receipt
            // register would buy the same answer twice.
            Self::DispatchAction | Self::RequestModelTurn | Self::RequestModelAttempt => {
                IdempotencyClass::Consequential
            }
            // A repeat means something different from the first call: a second
            // plan replaces the first, a second start is not a restart, a
            // second proposal is a second action to authorize, and a second
            // handover is a second interruption of a person — which is the
            // class `user.handover` already carries in the registry, for the
            // same reason. A second follow-up is a second question.
            Self::ProposeAction
            | Self::SetPlan
            | Self::StartTask
            | Self::RequestHandover
            | Self::RequestFieldValues
            | Self::FollowUp => IdempotencyClass::ConditionallyIdempotent,
            // Everything else settles state inside the reducer. A repeat with
            // the same key returns the original result and performs no effect.
            Self::SupplyFieldValues
            | Self::CreateTask
            | Self::EditScope
            | Self::AcceptInitialConsent
            | Self::RecordDiscoveryTab
            | Self::ApproveAction
            | Self::DenyAction
            | Self::PauseTask
            | Self::TakeOver
            | Self::PauseSettled
            | Self::ResumeTask
            | Self::CancelTask
            | Self::CancelSettled
            | Self::ExecutorStarted
            | Self::AdvanceStep
            | Self::RequestApproval
            | Self::RequestUserInput
            | Self::SupplyUserInput
            | Self::RequestPermission
            | Self::RecordPermissionResult
            | Self::CompleteHandover
            | Self::ExpireHandover
            | Self::RecordPolicyDecision
            | Self::RecordActionOutcome
            | Self::RecordToolJobOutcome
            | Self::RecordModelTurn
            | Self::RecordModelTurnGap
            | Self::RecordContextEviction
            | Self::ResultCandidateReady
            | Self::CompleteResultValidated
            | Self::PartialResultValidated
            | Self::ResumeForCorrection
            | Self::FailTask
            | Self::CorrectFact
            | Self::ExcludeSource
            | Self::RequestArtifact
            | Self::AcceptArtifact
            | Self::ExportArtifact => IdempotencyClass::IdempotentWrite,
        }
    }
}
