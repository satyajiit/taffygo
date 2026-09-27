// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Evaluating the guards the transition table names.
//!
//! The table (`crate::transition`) says *which* guard a cell depends on; this
//! module says whether it holds, and it is the only place that reads the
//! task's data to answer that question. Keeping the two apart is what lets the
//! table be walked exhaustively by a test without constructing a task.
//!
//! Every guard fails closed. A guard asked about a command it does not
//! describe answers `false`, so a mismatched pair refuses rather than passes.

mod tools;

use std::collections::BTreeMap;

use super::Reducer;
use crate::action::ActionState;
use crate::budget::BudgetKind;
use crate::command::Command;
use crate::ids::IdSource;
use crate::task::{BrowserSessionId, ConsentStage, TaskState, TaskTemplateId};
use crate::time::Clock;
use crate::tool;
use crate::transition::{Disposition, Guard, RefusalReason};
use bip_types::identity::TabId;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Every guard the cell names, in order; the first that does not hold
    /// refuses the command.
    ///
    /// One exception, and only while replaying: a bound on the task's own
    /// conduct that the journalled command crosses is held as history rather
    /// than refused, and named in the answer (decision 0235). Every other
    /// guard refuses a replay exactly as it refuses a live command.
    pub(super) fn check_guards(
        &self,
        cell: Disposition,
        command: &Command,
    ) -> Result<Option<Guard>, RefusalReason> {
        let mut held_as_history = None;
        for guard in cell.guards() {
            if self.guard_holds(*guard, command) {
                continue;
            }
            if self.replaying && super::replay::bounds_only_conduct(*guard, command) {
                held_as_history.get_or_insert(*guard);
                continue;
            }
            return Err(guard.refusal());
        }
        Ok(held_as_history)
    }

    fn guard_holds(&self, guard: Guard, command: &Command) -> bool {
        match guard {
            Guard::InitialConsentPending => {
                self.task.state != TaskState::AwaitingConsent
                    || self.task.consent_stage == Some(ConsentStage::Initial)
            }
            Guard::InTaskApprovalPending => {
                self.task.consent_stage == Some(ConsentStage::InTask)
                    && self.pending_action.is_some()
            }
            Guard::ApprovalStillCurrent => match command {
                Command::ApproveAction { still_current, .. } => *still_current,
                _ => false,
            },
            Guard::ResultIsComplete => match command {
                Command::CompleteResultValidated(result) => {
                    result.is_complete_for(self.task.kind())
                }
                _ => false,
            },
            Guard::NoActionWorkInFlight => self.no_action_work_in_flight(command),
            Guard::NoUnresolvedActionOutcomes => self.no_unresolved_action_outcomes(command),
            Guard::ToolAvailable => match command {
                Command::ProposeAction(proposal) => self.tool_available(proposal),
                _ => false,
            },
            // The loop guard reads only `is_abandoned`, never the running
            // count: the count is what the ledger uses to decide, and a guard
            // that re-derived the decision from it would be a second copy of
            // the threshold to keep in step with the first.
            Guard::NotLoopingOnRefusals => self.not_looping_on_refusals(command),
            Guard::ProposalNotAlreadyDispatched => match command {
                Command::ProposeAction(proposal) => {
                    !self.dispatched_keys.contains(&proposal.idempotency_key)
                }
                _ => false,
            },
            Guard::WithinBudget => self.within_budget(command),
            Guard::DiscoveryAuthorityMatches => match command {
                Command::RecordDiscoveryTab {
                    discovery_tab_id,
                    browser_session_id,
                } => self.discovery_authority_matches(discovery_tab_id, browser_session_id),
                _ => false,
            },
            Guard::NoModelTurnInFlight
            | Guard::ModelCallIsNext
            | Guard::ModelTurnMatches
            | Guard::EvictionAdvances => self.model_guard_holds(guard, command),
            Guard::ActionKnown | Guard::ActionOutcomeMatches | Guard::ActionAuthorized => {
                self.action_guard_holds(guard, command)
            }
            Guard::StepMayMove => match command {
                Command::AdvanceStep { plan_step_id, to } => self
                    .plan
                    .as_ref()
                    .is_some_and(|plan| plan.may_move(plan_step_id, *to, &self.verified_actions())),
                _ => false,
            },
            Guard::ArtifactAccepted => match command {
                Command::ExportArtifact { artifact_id, .. } => {
                    self.task.accepted_artifacts.contains(artifact_id)
                }
                _ => false,
            },
            Guard::ArtifactReady => match command {
                Command::AcceptArtifact { artifact_id } => {
                    self.artifacts.contains_key(artifact_id.as_str())
                        && self
                            .task
                            .terminal_result
                            .as_ref()
                            .is_some_and(|result| result.artifact_ids.contains(artifact_id))
                }
                _ => false,
            },
            Guard::PlanIsWellFormed => match command {
                Command::SetPlan(draft) => draft.validate().is_ok(),
                _ => false,
            },
            Guard::UserInputPending => self.user_input_pending(command),
            Guard::PendingPermissionMatches => match command {
                Command::RecordPermissionResult(result) => {
                    self.pending_permission.as_ref().is_some_and(|pending| {
                        pending.request_id() == result.request_id()
                            && pending.permission() == result.permission()
                    })
                }
                _ => false,
            },
            Guard::PendingHandoverMatches
            | Guard::ResumptionLeaseIsNew
            | Guard::FieldValueRequestMatches => self.handover_guard_holds(guard, command),
        }
    }

    pub(super) fn discovery_authority_matches(
        &self,
        discovery_tab_id: &TabId,
        browser_session_id: &BrowserSessionId,
    ) -> bool {
        let snapshot = &self.task.snapshot;
        let valid_tab = !discovery_tab_id.as_str().is_empty()
            && discovery_tab_id.as_str().len() <= crate::MAX_OBSERVATION_IDENTIFIER_BYTES
            && discovery_tab_id
                .as_str()
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'));
        // Holding a page is not a reason to refuse the tab. What this guard
        // still keeps is that the task owns exactly one discovery document:
        // `discovery_tab_id.is_none()` admits the first answer and refuses
        // every later one, so a second bootstrap cannot manufacture a second
        // blank tab (decision 0224).
        snapshot.template_id == TaskTemplateId::WebErrand
            && snapshot.source_discovery_enabled
            && snapshot.remaining_new_source_cap > 0
            && snapshot.discovery_tab_id.is_none()
            && snapshot.browser_session_id == *browser_session_id
            && valid_tab
    }

    fn within_budget(&self, command: &Command) -> bool {
        match command {
            Command::StartTask(preview) => self.start_scope_within_budget(preview),
            Command::ProposeAction(proposal) => proposal.budget_draw.is_none_or(|draw| {
                self.task
                    .ledger
                    .admit(draw.kind, draw.amount, &self.task.budgets, &self.defaults)
                    .is_ok()
            }),
            // A model turn's draw is not a field on the command: it is one
            // request, always, and reading the amount off the thing being
            // admitted would let a caller state what its own call costs.
            Command::RequestModelTurn { .. } => self.can_afford_a_model_turn(),
            Command::RequestModelAttempt { .. } => self.can_afford_model_attempt(),
            _ => false,
        }
    }

    fn start_scope_within_budget(&self, preview: &crate::task::ScopePreview) -> bool {
        let Ok(source_count) = u64::try_from(preview.sources.len()) else {
            return false;
        };
        let Some(total_sources) = source_count.checked_add(u64::from(preview.new_source_cap))
        else {
            return false;
        };
        let shape_is_valid = match self.task.snapshot.template_id {
            crate::task::TaskTemplateId::WebErrand
                if preview
                    .provider_route
                    .as_ref()
                    .map(crate::ProviderRouteId::as_str)
                    == Some(crate::REVIEWED_NO_MODEL_ROUTE_ID)
                    && self.task.snapshot.library_refresh.is_none() =>
            {
                self.task
                    .snapshot
                    .skill_version_id
                    .as_ref()
                    .is_some_and(|id| !id.0.is_empty())
                    && self.task.snapshot.builtin_skill.is_none()
                    && self.task.kind == crate::TaskKind::Errand
                    && self.task.control_mode == crate::ControlMode::Assistant
                    && preview.sources.len() == 1
                    && !preview.source_discovery_enabled
                    && preview.new_source_cap == 0
                    && preview.budgets.is_stated(BudgetKind::MaxModelRequests)
                    && preview
                        .budgets
                        .effective_limit(BudgetKind::MaxModelRequests, &self.defaults)
                        == 0
            }
            crate::task::TaskTemplateId::WebErrand => {
                preview.sources.len() <= 1
                    && preview.source_discovery_enabled
                    && (1..=crate::MAX_WEB_ERRAND_NEW_SOURCE_CAP).contains(&preview.new_source_cap)
            }
            crate::task::TaskTemplateId::BuildSourceTable
            | crate::task::TaskTemplateId::CompareProducts
            | crate::task::TaskTemplateId::SummarizeEvidence => {
                !preview.sources.is_empty()
                    && !preview.source_discovery_enabled
                    && preview.new_source_cap == 0
            }
        };
        shape_is_valid
            && preview.budgets.is_stated(BudgetKind::MaxSources)
            && preview
                .budgets
                .effective_limit(BudgetKind::MaxSources, &self.defaults)
                == total_sources
            && self
                .task
                .ledger
                .admit(
                    BudgetKind::MaxSources,
                    source_count,
                    &preview.budgets,
                    &self.defaults,
                )
                .is_ok()
    }

    /// The three guards about the actions this task holds.
    ///
    /// Apart from the rest for the same reason the model guards are: they are
    /// the only guards that read the action records, and each answers one
    /// question about one named action — is it known, is this outcome the one
    /// it is waiting for, and is it authorized to dispatch.
    fn action_guard_holds(&self, guard: Guard, command: &Command) -> bool {
        match guard {
            Guard::ActionKnown => match command {
                Command::RecordPolicyDecision { action_id, .. }
                | Command::DispatchAction { action_id, .. }
                | Command::RecordActionOutcome { action_id, .. }
                | Command::RecordToolJobOutcome { action_id, .. }
                | Command::RequestApproval { action_id } => {
                    self.actions.contains_key(action_id.as_str())
                }
                _ => false,
            },
            Guard::ActionOutcomeMatches => match command {
                Command::RecordActionOutcome { action_id, outcome } => {
                    self.action_outcome_matches(action_id, outcome)
                }
                Command::RecordToolJobOutcome {
                    action_id, job_id, ..
                } => self.tool_job_outcome_matches(action_id, job_id),
                _ => false,
            },
            Guard::ActionAuthorized => match command {
                Command::DispatchAction { action_id, .. } => self
                    .actions
                    .get(action_id.as_str())
                    .is_some_and(|action| action.state() == ActionState::Authorized),
                _ => false,
            },
            _ => false,
        }
    }

    /// The two guards about the handover this task may have open.
    ///
    /// Apart from the rest for the same reason the model guards are: they are
    /// the only two that read the handover, and together they are one question
    /// asked twice — is this completion the one the task is waiting for, and
    /// does it hand back authority under an identity of its own.
    fn handover_guard_holds(&self, guard: Guard, command: &Command) -> bool {
        match guard {
            // The answer must be to the request the task holds open, and it
            // must describe itself consistently: when it names the fields its
            // values were minted for, it names exactly one per value. A list
            // of any other length is a browser and a core disagreeing about
            // which value goes where, and a fill composed from it would put a
            // value into a field nobody chose (decision 0238). A restored
            // answer names no fields and is judged on its identity alone.
            Guard::FieldValueRequestMatches => match command {
                Command::SupplyFieldValues {
                    request_id,
                    supplied,
                    field_node_ids,
                    ..
                } => {
                    self.pending_field_values() == Some(request_id)
                        && field_node_ids
                            .as_ref()
                            .is_none_or(|fields| fields.len() == supplied.get())
                }
                _ => false,
            },
            Guard::PendingHandoverMatches => match command {
                Command::CompleteHandover(completion) => {
                    self.pending_handover.as_ref() == Some(completion.handover_id())
                }
                Command::ExpireHandover { handover_id } => {
                    self.pending_handover.as_ref() == Some(handover_id)
                }
                _ => false,
            },
            // Reads the completion and nothing else. The reducer holds no
            // lease and could not compare against one it remembered: the
            // browser reports both identities, and what is refused here is the
            // claim that resuming reuses the authority the handover gave up.
            Guard::ResumptionLeaseIsNew => match command {
                Command::CompleteHandover(completion) => completion.resumption_lease_is_new(),
                _ => false,
            },
            // Fails closed, exactly as the model guards' own fall-through
            // does, and reachable only by a routing mistake.
            Guard::InitialConsentPending
            | Guard::InTaskApprovalPending
            | Guard::ApprovalStillCurrent
            | Guard::ResultIsComplete
            | Guard::NoActionWorkInFlight
            | Guard::NoUnresolvedActionOutcomes
            | Guard::ToolAvailable
            | Guard::ProposalNotAlreadyDispatched
            | Guard::WithinBudget
            | Guard::DiscoveryAuthorityMatches
            | Guard::ActionKnown
            | Guard::ActionOutcomeMatches
            | Guard::ActionAuthorized
            | Guard::StepMayMove
            | Guard::ArtifactAccepted
            | Guard::ArtifactReady
            | Guard::PlanIsWellFormed
            | Guard::UserInputPending
            | Guard::PendingPermissionMatches
            | Guard::NotLoopingOnRefusals
            | Guard::NoModelTurnInFlight
            | Guard::ModelCallIsNext
            | Guard::ModelTurnMatches
            | Guard::EvictionAdvances => false,
        }
    }

    /// A generic completion may consume neither of the two exact waits.
    ///
    /// `WAITING_USER` is one state reached three ways — a value the task
    /// asked for, a native permission prompt, and a handover — and each of
    /// the other two has a completion of its own that carries facts this one
    /// does not. Letting `SupplyUserInput` end a handover would resume the
    /// assistant with no lease reported and no evidence that the person did
    /// anything.
    fn user_input_pending(&self, command: &Command) -> bool {
        matches!(command, Command::SupplyUserInput)
            && self.pending_permission.is_none()
            && self.pending_handover.is_none()
    }

    /// Whether the eviction boundary moves forward over turns that exist.
    fn eviction_advances(&self, command: &Command) -> bool {
        match command {
            Command::RecordContextEviction { through_turn } => {
                *through_turn < self.turns_started
                    && self
                        .evicted_through
                        .is_none_or(|already| *through_turn > already)
            }
            _ => false,
        }
    }

    /// Whether this proposal is not the same call refused its way to the
    /// ceiling.
    ///
    /// A command that proposes nothing holds trivially: this guard is about a
    /// call, and a command that makes none is not making that one again.
    fn not_looping_on_refusals(&self, command: &Command) -> bool {
        let Command::ProposeAction(proposal) = command else {
            return true;
        };
        // The same exception the allowlist makes, for the same reason. Three
        // refused handovers mean the way out is exactly what is not working,
        // and abandoning it there would leave the task holding the page with
        // no move left and no way to say so. A capability may be abandoned;
        // the exit may not — see `tool::UNCONDITIONAL_TOOLS`.
        //
        // The walk's own whole-document read is exempt on the other half of
        // the same argument: this register counts the model's calls, and that
        // read is not one. It is bounded where its own facts are, by
        // `MAX_SOURCE_BOOTSTRAP_READS` (decision 0196).
        //
        // The task's own fill of a held value is exempt for the same reason:
        // the register counts the model's calls, and a fill the task makes
        // once per position is bounded by its key (decision 0238).
        //
        // So is the scroll that brings a challenge into view before an ask:
        // the task makes it at most once per ask, by its key (decision 0240).
        tool::is_unconditional(proposal.tool_name())
            || crate::agent::is_task_move(proposal)
            || !self.refusals.is_abandoned(Self::call_of(proposal))
    }

    /// The three guards about the model call this task may be holding.
    ///
    /// Apart from the rest because they are the only guards that read the
    /// turn, and together because they are one question asked three ways: is
    /// the call the reducer is holding the call this command is about.
    fn model_guard_holds(&self, guard: Guard, command: &Command) -> bool {
        match guard {
            // Asks about the task rather than about the command, which is why
            // it has no match: three cells use it — a second turn, a settled
            // pause and a settled cancel — and the question is the same one
            // each time.
            Guard::NoModelTurnInFlight => !self.model_turn_in_flight(),
            Guard::EvictionAdvances => self.eviction_advances(command),
            Guard::ModelCallIsNext => match command {
                Command::RequestModelTurn { call_id } => *call_id == self.next_model_call_id(),
                _ => false,
            },
            Guard::ModelTurnMatches => match command {
                Command::RecordModelTurn { call_id, .. }
                | Command::RecordModelTurnGap { call_id, .. }
                | Command::RequestModelAttempt { call_id, .. } => self
                    .turn
                    .as_ref()
                    .is_some_and(|turn| turn.call_id() == call_id && turn.phase().is_in_flight()),
                _ => false,
            },
            // Fails closed, like every guard asked about something it does not
            // describe. Only the caller's own arm routes here, so reaching one
            // of these is a routing mistake rather than a decision.
            Guard::InitialConsentPending
            | Guard::InTaskApprovalPending
            | Guard::ApprovalStillCurrent
            | Guard::ResultIsComplete
            | Guard::NoActionWorkInFlight
            | Guard::NoUnresolvedActionOutcomes
            | Guard::ToolAvailable
            | Guard::ProposalNotAlreadyDispatched
            | Guard::WithinBudget
            | Guard::DiscoveryAuthorityMatches
            | Guard::ActionKnown
            | Guard::ActionOutcomeMatches
            | Guard::ActionAuthorized
            | Guard::StepMayMove
            | Guard::ArtifactAccepted
            | Guard::ArtifactReady
            | Guard::PlanIsWellFormed
            | Guard::UserInputPending
            | Guard::PendingPermissionMatches
            | Guard::PendingHandoverMatches
            | Guard::ResumptionLeaseIsNew
            | Guard::FieldValueRequestMatches
            | Guard::NotLoopingOnRefusals => false,
        }
    }

    fn no_action_work_in_flight(&self, command: &Command) -> bool {
        matches!(
            command,
            Command::PauseSettled
                | Command::CancelSettled
                | Command::CompleteResultValidated(_)
                | Command::PartialResultValidated(_)
        ) && self
            .actions
            .values()
            .all(|action| !action.state().may_have_reached_the_page())
    }

    fn no_unresolved_action_outcomes(&self, command: &Command) -> bool {
        matches!(
            command,
            Command::CompleteResultValidated(_) | Command::PartialResultValidated(_)
        ) && !self
            .actions
            .values()
            .any(super::actions::awaits_reconciliation)
    }

    fn verified_actions(&self) -> BTreeMap<String, bool> {
        self.actions
            .iter()
            .map(|(action_id, action)| (action_id.clone(), action.is_verified()))
            .collect()
    }
}
