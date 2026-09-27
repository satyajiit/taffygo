// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The fold itself: one command in, one committed transition or one refusal.
//!
//! Split from [`super`] so the reducer's *shape* and the reducer's *pipeline*
//! are read separately. `mod.rs` holds the type, its two register ceilings and
//! its constructor — what a task is made of. This holds the ordered sequence
//! every command goes through, which is the part with an argument in it:
//!
//! 1. a duplicate idempotency key returns the first delivery's result;
//! 2. the expected revision is checked;
//! 3. room is made for this command's own receipt;
//! 4. the transition table decides;
//! 5. the guards decide;
//! 6. and only then is anything written.
//!
//! [`Reducer::execute`] stays here rather than in a family module because it
//! is the exhaustive match that proves every command has exactly one handler.
//! Adding a command is a compile error in this file until it has one.

use super::outcome::{entry_event, phase_after, phase_for, Outcome};
use super::{Accepted, CommandReceipt, Reducer, Refusal, MAX_COMMAND_RECEIPTS};
use crate::command::{Command, CommandEnvelope, CommandKind};
use crate::event::TaskEvent;
use crate::ids::IdSource;
use crate::task::TaskState;
use crate::time::Clock;
use crate::transition::{disposition, Guard, RefusalReason};

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Applies a command.
    ///
    /// The order is fixed: a duplicate key returns the original result first,
    /// then the expected revision is checked, then room is made for this
    /// command's own receipt, then the table decides, then the guards, and
    /// only then is anything written. Making room before the table runs is
    /// what keeps the receipt register's ceiling a refusal rather than a
    /// half-applied command with no receipt to answer its retry.
    pub fn apply(&mut self, envelope: CommandEnvelope) -> Result<Accepted, Refusal> {
        self.fold(envelope).map(|(accepted, _)| accepted)
    }

    /// [`Self::apply`] for a command the journal already holds, saying which
    /// bound on the task's conduct, if any, it was applied over.
    ///
    /// Only replay calls this, and only while replaying does the answer ever
    /// name a bound: live, every guard refuses exactly as it did (decision
    /// 0235).
    pub(super) fn apply_journalled(
        &mut self,
        envelope: CommandEnvelope,
    ) -> Result<(Accepted, Option<Guard>), Refusal> {
        self.fold(envelope)
    }

    fn fold(&mut self, envelope: CommandEnvelope) -> Result<(Accepted, Option<Guard>), Refusal> {
        if let Some(receipt) = self.receipts.get(&envelope.idempotency_key) {
            let mut replayed = receipt.accepted.clone();
            replayed.duplicate = true;
            replayed.effects.clear();
            return Ok((replayed, None));
        }

        let from = self.task.state;
        let command = envelope.kind();
        let expected_revision = envelope.expected_revision;
        if expected_revision != self.task.revision {
            return Err(self.refuse(RefusalReason::RevisionConflict, command));
        }
        if !self.make_room_for_receipt() {
            return Err(self.refuse(RefusalReason::ReceiptRegisterFull, command));
        }

        let cell = disposition(from, command);
        if let Some(reason) = cell.refusal() {
            return Err(self.refuse(reason, command));
        }
        let held_as_history = match self.check_guards(cell, &envelope.command) {
            Ok(held) => held,
            Err(reason) => return Err(self.refuse(reason, command)),
        };

        let outcome = match self.execute(&envelope.command, command) {
            Ok(outcome) => outcome,
            Err(reason) => return Err(self.refuse(reason, command)),
        };
        let loop_phase = phase_after(&envelope.command, |action_id| {
            self.actions
                .get(action_id.as_str())
                .map(|action| action.proposal().action_class())
        });

        let now = self.clock.now_utc();
        let key = envelope.idempotency_key.clone();
        let trace = envelope.trace_id.clone();
        self.journal.append_command(envelope, now);

        let mut events = outcome.events;
        if let (Some(target), Some(reason)) = (outcome.target, outcome.reason) {
            self.task.state = target;
            self.task.execution_phase = phase_for(target);
            self.task.state_reason = Some(reason);
            events.push(TaskEvent::transition(
                entry_event(target),
                command,
                from,
                target,
                reason,
            ));
        }
        // Inside RUNNING the loop's own command says what the runtime is doing;
        // a transition's entry phase stands only until the next such command.
        if let (Some(phase), TaskState::Running) = (loop_phase, self.task.state) {
            self.task.execution_phase = Some(phase);
        }
        for event in &events {
            self.task.revision =
                self.journal
                    .append_event(event.clone(), trace.clone(), key.clone(), now);
        }
        self.task.updated_at = now;

        let effects = if self.replaying {
            Vec::new()
        } else {
            outcome.effects
        };
        let accepted = Accepted {
            from,
            to: self.task.state,
            revision: self.task.revision,
            events,
            effects,
            duplicate: false,
        };
        self.receipts.insert(
            key,
            CommandReceipt {
                expected_revision,
                accepted: accepted.clone(),
            },
        );
        Ok((accepted, held_as_history))
    }

    /// Frees a receipt slot when the register is at its ceiling.
    ///
    /// Two floors, both of which drop only receipts whose commands have
    /// nothing left to protect, and neither of which is a guess.
    ///
    /// **The revision.** A command written against revision `E` was accepted
    /// only because `E` was the task's revision at the time; once the revision
    /// has moved past `E`, a second delivery of that command meets
    /// [`RefusalReason::RevisionConflict`] before [`Reducer::apply`] ever
    /// looks its receipt up. The revision only ever increases, so this is a
    /// floor rather than a guess, and it is the whole reason the receipt
    /// register may evict while the action register may not.
    ///
    /// **The empty result.** A command that moved no state, journalled no
    /// event, and returned no effect did nothing a second delivery could do
    /// twice. Without this floor a caller could wedge a task permanently:
    /// pausing a task that is already settling is exactly such a command, so
    /// [`MAX_COMMAND_RECEIPTS`] of them under distinct keys would fill the
    /// register with receipts the first floor cannot touch — every one of them
    /// written against the revision the task is still sitting at — and the
    /// settlement that would have moved it on would be the command refused.
    ///
    /// Returns `false` only when neither floor frees a slot. Every event
    /// advances the revision, so a receipt that survives both floors belongs
    /// to a command that changed the task while leaving its revision where it
    /// was, which the fold has no path to produce. It stays as the honest
    /// answer if one ever appears: refusing is safe, and dropping a receipt
    /// that still answers for something is not.
    fn make_room_for_receipt(&mut self) -> bool {
        if self.receipts.len() < MAX_COMMAND_RECEIPTS {
            return true;
        }
        let floor = self.task.revision;
        self.receipts
            .retain(|_, receipt| receipt.expected_revision >= floor);
        if self.receipts.len() < MAX_COMMAND_RECEIPTS {
            return true;
        }
        self.receipts.retain(|_, receipt| {
            let result = &receipt.accepted;
            result.from != result.to || !result.events.is_empty() || !result.effects.is_empty()
        });
        self.receipts.len() < MAX_COMMAND_RECEIPTS
    }

    fn refuse(&self, reason: RefusalReason, command: CommandKind) -> Refusal {
        Refusal {
            reason,
            state: self.task.state,
            command,
            revision: self.task.revision,
        }
    }

    /// Routes one command to the handler that owns it.
    ///
    /// The match is exhaustive and every arm is one call, so this function is
    /// the *index* of the command surface and nothing else. Any logic that
    /// appears here belongs in the family module the arm points at.
    fn execute(&mut self, command: &Command, kind: CommandKind) -> Result<Outcome, RefusalReason> {
        if matches!(
            kind,
            CommandKind::ResultCandidateReady
                | CommandKind::CompleteResultValidated
                | CommandKind::PartialResultValidated
                | CommandKind::FailTask
                | CommandKind::CorrectFact
                | CommandKind::ExcludeSource
                | CommandKind::RequestArtifact
                | CommandKind::AcceptArtifact
                | CommandKind::ExportArtifact
        ) {
            return self.execute_result_command(command, kind);
        }
        if matches!(
            kind,
            CommandKind::RequestModelTurn
                | CommandKind::RequestModelAttempt
                | CommandKind::RecordModelTurn
                | CommandKind::RecordModelTurnGap
                | CommandKind::RecordContextEviction
        ) {
            return self.execute_model_command(command, kind);
        }
        self.execute_non_result_command(command, kind)
    }

    fn execute_non_result_command(
        &mut self,
        command: &Command,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        match command {
            // Refused by the table in every state; the constructor creates.
            Command::CreateTask => Err(RefusalReason::TaskAlreadyExists),

            Command::EditScope(scope) => Ok(self.on_edit_scope(scope, kind)),
            Command::StartTask(preview) => Ok(self.on_start_task(preview, kind)),
            Command::AcceptInitialConsent(_) => Ok(self.on_accept_initial_consent(kind)),
            Command::RecordDiscoveryTab {
                discovery_tab_id,
                browser_session_id,
            } => self.on_record_discovery_tab(discovery_tab_id, browser_session_id, kind),
            Command::ApproveAction {
                approval,
                expires_at_monotonic_ms,
                expires_at_utc_ms,
                browser_session_id,
                ..
            } => Ok(self.on_approve_action(
                approval,
                *expires_at_monotonic_ms,
                *expires_at_utc_ms,
                browser_session_id,
                kind,
            )),
            Command::DenyAction { .. } => Ok(self.on_deny_action(kind)),
            Command::PauseTask { cause } => Ok(self.on_pause_task(*cause)),
            Command::TakeOver => Ok(self.on_take_over(kind)),
            Command::PauseSettled => Ok(self.on_pause_settled()),
            Command::ResumeTask => Ok(self.on_resume_task(kind)),
            Command::CancelTask => Ok(self.on_cancel_task()),
            Command::CancelSettled => Ok(self.on_cancel_settled()),
            Command::ExecutorStarted => Ok(Self::on_executor_started()),
            Command::RequestUserInput => Ok(self.on_request_user_input()),
            Command::SupplyUserInput => Ok(self.on_supply_user_input(kind)),
            Command::RequestPermission(request) => Ok(self.on_request_permission(request, kind)),
            Command::RecordPermissionResult(result) => {
                Ok(self.on_record_permission_result(result, kind))
            }
            Command::RequestHandover { handover_id } => {
                Ok(self.on_request_handover(handover_id, kind))
            }
            Command::RequestFieldValues { .. } | Command::SupplyFieldValues { .. } => {
                self.execute_field_values_command(command, kind)
            }
            Command::CompleteHandover(completion) => {
                Ok(self.on_complete_handover(completion, kind))
            }
            Command::ExpireHandover { handover_id } => {
                Ok(self.on_expire_handover(handover_id, kind))
            }
            Command::ResumeForCorrection => Ok(Self::on_resume_for_correction()),
            Command::FollowUp => Ok(self.on_follow_up(kind)),

            Command::SetPlan(draft) => self.on_set_plan(draft, kind),
            Command::AdvanceStep { plan_step_id, to } => {
                self.on_advance_step(plan_step_id, *to, kind)
            }

            Command::RequestApproval { action_id } => Ok(self.on_request_approval(action_id, kind)),
            Command::ProposeAction(proposal) => self.on_propose_action(proposal, kind),
            Command::RecordPolicyDecision {
                action_id,
                decision,
                dispatch_id,
            } => self.on_record_policy_decision(action_id, decision, dispatch_id.as_ref(), kind),
            Command::DispatchAction {
                action_id,
                dispatch_id,
            } => self.on_dispatch_action(action_id, dispatch_id, kind),
            Command::RecordActionOutcome { action_id, outcome } => {
                self.on_record_action_outcome(action_id, outcome.as_ref(), kind)
            }
            Command::RecordToolJobOutcome {
                action_id,
                job_id,
                outcome,
            } => self.on_record_tool_job_outcome(action_id, job_id, outcome.as_ref(), kind),

            Command::ResultCandidateReady
            | Command::CompleteResultValidated(_)
            | Command::PartialResultValidated(_)
            | Command::FailTask { .. }
            | Command::CorrectFact { .. }
            | Command::ExcludeSource { .. }
            | Command::RequestArtifact { .. }
            | Command::AcceptArtifact { .. }
            | Command::ExportArtifact { .. }
            | Command::RequestModelTurn { .. }
            | Command::RequestModelAttempt { .. }
            | Command::RecordModelTurn { .. }
            | Command::RecordModelTurnGap { .. }
            | Command::RecordContextEviction { .. } => Err(RefusalReason::JournalRefused),
        }
    }

    /// The two halves of one request for values (decisions 0088 and 0238).
    fn execute_field_values_command(
        &mut self,
        command: &Command,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        match command {
            Command::RequestFieldValues {
                request_id,
                tab_id,
                node_id,
                companion_node_ids,
            } => Ok(self.on_request_field_values(
                request_id,
                tab_id,
                node_id,
                companion_node_ids,
                kind,
            )),
            Command::SupplyFieldValues {
                request_id,
                supplied,
                outcome,
                field_node_ids,
            } => Ok(self.on_supply_field_values(
                request_id,
                *supplied,
                *outcome,
                field_node_ids.as_ref(),
                kind,
            )),
            _ => Err(RefusalReason::JournalRefused),
        }
    }
}
