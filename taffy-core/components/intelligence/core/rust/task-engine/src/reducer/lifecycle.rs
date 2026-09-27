// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The commands that move a task through consent, control, and settlement.
//!
//! One responsibility: the *user's* relationship with the task — what it is
//! allowed to read, when it starts, and every way it is held or stopped.
//! Nothing here plans, proposes, dispatches, or validates a result.
//!
//! The invariant this module carries is the third one in the crate's list:
//! **pausing and stopping revoke authority before waiting on remote work.**
//! Every settling edge returns [`crate::effect::settle`], which orders
//! revocation ahead of the wait, so no handler here can accidentally wait
//! first.

use super::outcome::{revocation_for_pause, Outcome};
use super::Reducer;
use crate::agent::TurnPhase;
use crate::authority::RevocationReason;
use crate::budget::BudgetKind;
use crate::command::{CommandKind, PauseCause};
use crate::effect::{hand_over, settle, Effect};
use crate::event::{EventKind, EventSubject, TaskEvent};
use crate::field_values::{
    FieldNodeIds, FieldValueAskOutcome, FieldValueRequestId, HeldValuePlacement,
    SuppliedFieldValues, SuppliedValueCount,
};
use crate::handover::{HandoverCompletion, HandoverId};
use crate::ids::IdSource;
use crate::permission::{PermissionRequest, PermissionResult};
use crate::task::{
    BrowserSessionId, ConsentStage, ScopePreview, SourceScope, StateReason, TaskActivityKind,
    TaskState, TaskTemplateId,
};
use crate::time::{Clock, UtcMillis};
use bip_types::identity::{ApprovalReceiptReference, MonotonicMillis, SemanticNodeId, TabId};

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Replaces the scope. Editing it while consent is pending returns the
    /// task to `DRAFT`: the preview the user was shown no longer describes
    /// what they would be consenting to.
    pub(super) fn on_edit_scope(&mut self, scope: &SourceScope, kind: CommandKind) -> Outcome {
        self.task.scope = scope.clone();
        self.task.consent_stage = None;
        let events = vec![TaskEvent::record(EventKind::SourceScopeSet, kind)];
        if self.task.state == TaskState::AwaitingConsent {
            Outcome::moves(TaskState::Draft, StateReason::ScopeEdited).with_events(events)
        } else {
            Outcome::recorded(events, Vec::new())
        }
    }

    /// Freezes the previewed scope, budgets, and route, and asks for the
    /// initial consent.
    pub(super) fn on_start_task(&mut self, preview: &ScopePreview, kind: CommandKind) -> Outcome {
        self.task.scope = preview.scope.clone();
        self.task.budgets = preview.budgets.clone();
        self.task
            .snapshot
            .consented_sources
            .clone_from(&preview.sources);
        self.task.consented_sources.clone_from(&preview.sources);
        self.task.snapshot.source_discovery_enabled = preview.source_discovery_enabled;
        self.task.snapshot.remaining_new_source_cap = preview.new_source_cap;
        self.task.snapshot.discovery_tab_id = None;
        self.task
            .snapshot
            .provider_route
            .clone_from(&preview.provider_route);
        self.task.consent_stage = Some(ConsentStage::Initial);
        let initial_source_count = u64::try_from(preview.sources.len()).unwrap_or(u64::MAX);
        self.task
            .ledger
            .charge(BudgetKind::MaxSources, initial_source_count);
        let mut events = vec![
            TaskEvent::record(EventKind::SourceScopeSet, kind),
            TaskEvent::record(EventKind::ProviderRouteSelected, kind),
        ];
        if initial_source_count > 0 {
            events.push(TaskEvent::record(EventKind::BudgetCharged, kind));
        }
        Outcome::moves(TaskState::AwaitingConsent, StateReason::PreviewReady).with_events(events)
    }

    /// The user accepted the initial consent, so the task may be queued.
    pub(super) fn on_accept_initial_consent(&mut self, kind: CommandKind) -> Outcome {
        self.task.consent_stage = None;
        // Every errand whose consent granted discovery gets the blank tab,
        // whether or not it also starts holding a page. The two are
        // independent grants and always were: "read this page" and "you may
        // open up to N sites" are different sentences in the same sheet, and
        // gating the tab on holding no page made the second one unspendable
        // for the ordinary case — an errand asked from a page (decision 0224).
        let effects = if self.task.snapshot.template_id == TaskTemplateId::WebErrand
            && self.task.snapshot.source_discovery_enabled
            && self.task.snapshot.remaining_new_source_cap > 0
        {
            vec![Effect::PrepareDiscoveryTab {
                browser_session_id: self.task.snapshot.browser_session_id.clone(),
                remaining_new_source_cap: self.task.snapshot.remaining_new_source_cap,
            }]
        } else {
            Vec::new()
        };
        Outcome::moves(TaskState::Queued, StateReason::InitialConsentAccepted)
            .with_events(vec![TaskEvent::record(EventKind::ApprovalDecided, kind)])
            .with_effects(effects)
    }

    /// Records the content-free tab identity returned by the zero-source
    /// discovery bootstrap.
    ///
    /// A tab is not a source. This handler never changes `scope` or
    /// `consented_sources`; only a verified action outcome may do that.
    pub(super) fn on_record_discovery_tab(
        &mut self,
        discovery_tab_id: &TabId,
        browser_session_id: &BrowserSessionId,
        kind: CommandKind,
    ) -> Result<Outcome, crate::transition::RefusalReason> {
        if !self.discovery_authority_matches(discovery_tab_id, browser_session_id) {
            return Err(crate::transition::RefusalReason::DiscoveryAuthorityMismatch);
        }
        self.task.snapshot.discovery_tab_id = Some(discovery_tab_id.clone());
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::DiscoveryTabPrepared, kind)
                .about(EventSubject::DiscoveryTab(discovery_tab_id.clone()))],
            Vec::new(),
        ))
    }

    /// The user approved the pending action, so work resumes.
    pub(super) fn on_approve_action(
        &mut self,
        approval: &ApprovalReceiptReference,
        expires_at_monotonic_ms: u64,
        expires_at_utc_ms: u64,
        browser_session_id: &BrowserSessionId,
        kind: CommandKind,
    ) -> Outcome {
        self.task.consent_stage = None;
        let subject = self.pending_action.take();
        let mut event = TaskEvent::record(EventKind::ApprovalDecided, kind);
        let mut effects = Vec::new();
        if let Some(action_id) = subject {
            if let Some(action) = self.actions.get_mut(action_id.as_str()) {
                action.record_approval(
                    approval.clone(),
                    MonotonicMillis(expires_at_monotonic_ms),
                    UtcMillis(expires_at_utc_ms),
                    browser_session_id.clone(),
                );
                effects.push(Effect::AskPolicy {
                    action_id: action_id.clone(),
                });
            }
            event = event.about(EventSubject::Action(action_id));
        }
        // The closing half of the wait `on_request_approval` opened. Both
        // answers are the same step, because what the record is about is that
        // the person was reached for and came back — which of the two buttons
        // they pressed is the task's next state and the header already says it.
        self.note(TaskActivityKind::YouAnswered, None, 0);
        Outcome::moves(TaskState::Running, StateReason::ApprovalAccepted)
            .with_events(vec![event])
            .with_effects(effects)
    }

    /// The user denied the pending action. The action is cancelled if it never
    /// left, and the task settles rather than carrying on without it.
    pub(super) fn on_deny_action(&mut self, kind: CommandKind) -> Outcome {
        self.task.consent_stage = None;
        self.cancel_pending_action();
        self.supplied_field_values = None;
        self.note(TaskActivityKind::YouAnswered, None, 0);
        Outcome::moves(TaskState::Pausing, StateReason::ApprovalDenied)
            .with_events(vec![TaskEvent::record(EventKind::ApprovalDecided, kind)])
            .with_effects(settle(
                RevocationReason::UserTookOver,
                CommandKind::PauseSettled,
            ))
    }

    /// Holds the task. A task that is already settling or held does nothing,
    /// so a repeated pause cannot restart the settlement it is waiting on.
    pub(super) fn on_pause_task(&mut self, cause: PauseCause) -> Outcome {
        if self.is_settling_or_held() {
            return Outcome::nothing();
        }
        let reason = match cause {
            PauseCause::User => StateReason::UserPaused,
            PauseCause::BackgroundRestricted => StateReason::BackgroundRestricted,
            PauseCause::ProviderLimit
            | PauseCause::ProviderBusy
            | PauseCause::Offline
            | PauseCause::NoAnswer => StateReason::ProviderPaused,
        };
        self.task.consent_stage = None;
        self.cancel_pending_action();
        self.pending_permission = None;
        self.pending_handover = None;
        self.pending_field_values = None;
        self.supplied_field_values = None;
        self.pause_cause = Some(cause);
        Outcome::moves(TaskState::Pausing, reason).with_effects(settle(
            revocation_for_pause(cause),
            CommandKind::PauseSettled,
        ))
    }

    /// Why the task is paused, while it is. A surface reads it to say what a
    /// resume would try again; a task that is not paused holds nothing here.
    pub const fn pause_cause(&self) -> Option<PauseCause> {
        self.pause_cause
    }

    /// The user took the tab. Authority is revoked before anything waits.
    pub(super) fn on_take_over(&mut self, kind: CommandKind) -> Outcome {
        if self.is_settling_or_held() {
            return Outcome::nothing();
        }
        self.task.consent_stage = None;
        self.cancel_pending_action();
        self.pending_permission = None;
        self.pending_handover = None;
        self.pending_field_values = None;
        self.supplied_field_values = None;
        self.note(TaskActivityKind::YouTookOver, None, 0);
        Outcome::moves(TaskState::Pausing, StateReason::UserTookOver)
            .with_events(vec![TaskEvent::record(EventKind::UserTookOver, kind)])
            .with_effects(settle(
                RevocationReason::UserTookOver,
                CommandKind::PauseSettled,
            ))
    }

    /// Settlement finished: the task is held and holds no authority.
    pub(super) fn on_pause_settled(&mut self) -> Outcome {
        // Resume supersedes the old plan, so none of its unspent one-use
        // approvals may become live again. In-flight actions stay in their
        // uncertainty-bearing state until their exact terminal arrives.
        for action in self.actions.values_mut() {
            action.cancel_if_undispatched();
        }
        Outcome::moves(TaskState::Paused, StateReason::SettlingComplete)
    }

    /// Resume revalidates rather than claiming execution continued, so the
    /// plan the task was working from is superseded here.
    pub(super) fn on_resume_task(&mut self, kind: CommandKind) -> Outcome {
        if let Some(plan) = self.plan.as_mut() {
            plan.supersede();
        }
        // A turn that ended in a gap is over: nothing is in flight and no
        // reply will come. Left in place, the agent table would read the gap
        // again on the first pass after the resume and fail the task for the
        // provider answer that paused it. Dropping it is what makes a resume
        // ask the model again, which is the only thing a resume can mean.
        if self
            .turn
            .as_ref()
            .is_some_and(|turn| matches!(turn.phase(), TurnPhase::Gap(_)))
        {
            self.turn = None;
        }
        self.pause_cause = None;
        Outcome::moves(TaskState::Queued, StateReason::ResumeRequested)
            .with_events(vec![TaskEvent::record(EventKind::TaskResumed, kind)])
    }

    /// Stops the task. A draft holds no authority and has nothing in flight,
    /// so it ends immediately; anything else settles first.
    pub(super) fn on_cancel_task(&mut self) -> Outcome {
        if self.task.state == TaskState::Cancelling {
            return Outcome::nothing();
        }
        self.cancel_pending_action();
        self.pending_permission = None;
        self.pending_handover = None;
        self.pending_field_values = None;
        self.supplied_field_values = None;
        self.task.consent_stage = None;
        if self.task.state == TaskState::Draft {
            return Outcome::moves(TaskState::Cancelled, StateReason::Discarded);
        }
        Outcome::moves(TaskState::Cancelling, StateReason::StopRequested).with_effects(settle(
            RevocationReason::TaskCancelled,
            CommandKind::CancelSettled,
        ))
    }

    /// Settlement finished after a stop: undispatched actions are cancelled,
    /// the plan is superseded, and the tabs go back to the user.
    pub(super) fn on_cancel_settled(&mut self) -> Outcome {
        for action in self.actions.values_mut() {
            action.cancel_if_undispatched();
        }
        if let Some(plan) = self.plan.as_mut() {
            plan.supersede();
        }
        Outcome::moves(TaskState::Cancelled, StateReason::SettlingComplete)
            .with_effects(vec![Effect::ReleaseTaskTabs])
    }

    fn cancel_pending_action(&mut self) {
        if let Some(action_id) = self.pending_action.take() {
            if let Some(action) = self.actions.get_mut(action_id.as_str()) {
                action.cancel_if_undispatched();
            }
        }
    }

    /// An executor picked the task up.
    pub(super) fn on_executor_started() -> Outcome {
        Outcome::moves(TaskState::Running, StateReason::ExecutorStarted)
    }

    /// A source or an input is missing, so the task waits for the user.
    pub(super) fn on_request_user_input(&mut self) -> Outcome {
        // A turn that has reached for the person has finished reaching for the
        // page. Recording it here rather than in the agent table is what makes
        // it replayable: the flag is re-derived from the journalled command
        // instead of restored from a snapshot of a decision, and without it
        // the loop would walk the same reply on the way back in and ask the
        // same question forever.
        self.note_turn_asked_person();
        // No host: what is missing is a source or an input the task asked for
        // in words, and no tab is named by the command that asked. A step
        // declines to name a host rather than guessing one from the last page
        // it happened to touch.
        self.note(TaskActivityKind::AskedYou, None, 0);
        Outcome::moves(TaskState::WaitingUser, StateReason::UserInputNeeded)
    }

    /// The user supplied what was missing.
    pub(super) fn on_supply_user_input(&mut self, kind: CommandKind) -> Outcome {
        // The person answered, which is a thing the task did not hold before
        // it asked (decision 0233).
        self.note_progress();
        self.note(TaskActivityKind::YouAnswered, None, 0);
        Outcome::moves(TaskState::Running, StateReason::UserInputSupplied)
            .with_events(vec![TaskEvent::record(EventKind::UserInputSupplied, kind)])
    }

    /// Commits exact native permission facts before opening any platform UI.
    pub(super) fn on_request_permission(
        &mut self,
        request: &PermissionRequest,
        kind: CommandKind,
    ) -> Outcome {
        self.pending_permission = Some(request.clone());
        // No host, and deliberately: a platform permission is asked of the
        // person about the device rather than about a page, and naming the
        // page that happened to provoke it would read as the page asking.
        self.note(TaskActivityKind::AskedYou, None, 0);
        Outcome::moves(TaskState::WaitingUser, StateReason::PermissionRequested)
            .with_events(vec![TaskEvent::record(
                EventKind::PermissionRequested,
                kind,
            )
            .about(EventSubject::PermissionRequest(
                request.request_id().clone(),
            ))])
            .with_effects(vec![Effect::RequestPermission {
                request_id: request.request_id().clone(),
                permission: request.permission(),
                deadline_monotonic_ms: request.deadline_monotonic_ms(),
                deadline_utc_ms: request.deadline_utc_ms(),
                browser_session_id: request.browser_session_id().clone(),
            }])
    }

    /// Records one terminal decision only after the guard matched the exact
    /// pending request identity and closed permission kind.
    pub(super) fn on_record_permission_result(
        &mut self,
        result: &PermissionResult,
        kind: CommandKind,
    ) -> Outcome {
        self.pending_permission = None;
        self.note(TaskActivityKind::YouAnswered, None, 0);
        Outcome::moves(TaskState::Running, StateReason::PermissionDecided)
            .with_events(vec![TaskEvent::record(EventKind::PermissionDecided, kind)
                .about(EventSubject::PermissionRequest(result.request_id().clone()))])
    }

    /// The assistant stopped and gave the page to the person.
    ///
    /// Two things happen here and their order is the point. The events and the
    /// state record that the task is waiting, and the effects revoke authority
    /// *before* anything waits — [`hand_over`] builds that pair, so this
    /// handler cannot get it wrong. The person is about to type into this tab
    /// and the assistant must not be holding mutation authority over it while
    /// they do.
    ///
    /// The turn is marked as having reached the person for the same reason
    /// [`Self::on_request_user_input`] marks it: without it the agent loop
    /// walks the same reply on the way back in and hands over again, forever.
    pub(super) fn on_request_handover(
        &mut self,
        handover_id: &HandoverId,
        kind: CommandKind,
    ) -> Outcome {
        self.pending_handover = Some(handover_id.clone());
        self.note_turn_asked_person();
        // The handover names no tab — its identity is minted from the model
        // call, not from a page — so the step names no host either. The
        // sentence the surface draws is the one without it.
        self.note(TaskActivityKind::HandedBack, None, 0);
        Outcome::moves(TaskState::WaitingUser, StateReason::HandoverRequested)
            .with_events(vec![TaskEvent::record(EventKind::HandoverRequested, kind)
                .about(EventSubject::Handover(handover_id.clone()))])
            .with_effects(hand_over(handover_id.clone()))
    }

    /// The person is asked to fill a form in (decision 0088).
    ///
    /// Deliberately not a handover: no authority is revoked, because the
    /// person types into a surface Taffy draws rather than into the page, and
    /// the assistant carries on in the same tab with the same lease once the
    /// values are held. The state it lands in is the same `WAITING_USER` every
    /// other way of stopping for a person lands in — an input request is a
    /// richer surface over the state, not a new one.
    ///
    /// The companions travel to the browser and are not held here: which of
    /// them the sheet showed comes back with the answer, in the order the
    /// values were minted, and that is the list a fill is composed from
    /// (decision 0238).
    pub(super) fn on_request_field_values(
        &mut self,
        request_id: &FieldValueRequestId,
        tab_id: &TabId,
        node_id: &SemanticNodeId,
        companion_node_ids: &FieldNodeIds,
        kind: CommandKind,
    ) -> Outcome {
        self.pending_field_values = Some(super::PendingFieldValues {
            request_id: request_id.clone(),
            tab_id: tab_id.clone(),
        });
        // A later request supersedes the positions from the earlier one. The
        // browser may still hold an unspent record until its own deadline,
        // but this reducer will never compose a new action naming it.
        self.supplied_field_values = None;
        // The same reason `on_request_user_input` records it: a turn that has
        // reached for the person has finished reaching for the page, and the
        // flag is re-derived from the journalled command rather than restored
        // from a snapshot of a decision.
        self.note_turn_asked_person();
        // This one does know its page: the request names the tab the field is
        // in, and the task may only ever have been consented to it.
        let host = self.host_of_tab(tab_id);
        self.note(TaskActivityKind::AskedYou, host, 0);
        // No event, for the same reason `on_request_user_input` records none:
        // the durable fact is the journalled command that asked, and a second
        // record of the same fact is not more evidence.
        let _ = kind;
        Outcome::moves(TaskState::WaitingUser, StateReason::UserInputNeeded).with_effects(vec![
            Effect::RequestFieldValues {
                request_id: request_id.clone(),
                tab_id: tab_id.clone(),
                node_id: node_id.clone(),
                companion_node_ids: companion_node_ids.clone(),
            },
        ])
    }

    /// The person answered, or the browser gave up asking.
    ///
    /// The count and what became of the ask are all there is. What the person
    /// typed was minted into the browser's vault and never entered this
    /// process, so there is nothing here to record about it and nothing for
    /// the event to be about.
    ///
    /// The outcome is held rather than decided on: a zero count reaches this
    /// reducer for six different reasons and each wants a different next move,
    /// and the party that knows which is the browser (decision 0215). It is
    /// `None` when this command was rebuilt from the journal.
    ///
    /// So are the fields, and for the same reason (decision 0238). When they
    /// are present they are held with the tab the request named, which is what
    /// lets the decision table put each value into its field before the model
    /// is asked for another turn; the guard has already refused a list whose
    /// length is not the count.
    pub(super) fn on_supply_field_values(
        &mut self,
        request_id: &FieldValueRequestId,
        supplied: SuppliedValueCount,
        outcome: Option<FieldValueAskOutcome>,
        field_node_ids: Option<&FieldNodeIds>,
        kind: CommandKind,
    ) -> Outcome {
        let tab_id = self
            .pending_field_values
            .take()
            .map(|pending| pending.tab_id);
        // Before the record is stored, because the count is what it reads and
        // the run it advances is about the asks and not about this one
        // (decision 0216).
        self.note_value_ask_answer(supplied);
        let placement = tab_id
            .zip(field_node_ids)
            .filter(|(_, fields)| !fields.is_empty())
            .map(|(tab_id, fields)| HeldValuePlacement::new(tab_id, fields.clone()));
        self.supplied_field_values = Some(
            SuppliedFieldValues::new(request_id.clone(), supplied, outcome)
                .with_placement(placement),
        );
        // The count stays out of the step. How many boxes a person filled in is
        // not what the timeline is about, and a count beside "You answered"
        // reads as a count of facts everywhere else in this list.
        self.note(TaskActivityKind::YouAnswered, None, 0);
        Outcome::moves(TaskState::Running, StateReason::UserInputSupplied)
            .with_events(vec![TaskEvent::record(EventKind::UserInputSupplied, kind)])
    }

    /// The person came back, so the assistant resumes.
    ///
    /// The event is about the *lease*, not about the handover. The handover
    /// identity is already durable — the journal keeps the command envelope
    /// that named it — and what the journal does not otherwise hold is the
    /// identity the assistant resumes under. Recording it here is what lets a
    /// reader draw the line: everything under the earlier lease was the
    /// assistant's, everything between the two was the person's, and
    /// everything under this one is the assistant's again.
    ///
    /// The evidence that the person actually did anything travels in the
    /// command as a bounded count and stays there. It is not promoted to a
    /// state or a reason, because nothing decides anything from it — see
    /// [`crate::handover::PersonInput`].
    pub(super) fn on_complete_handover(
        &mut self,
        completion: &HandoverCompletion,
        kind: CommandKind,
    ) -> Outcome {
        self.pending_handover = None;
        // An errand that needed a sign-in, an OTP or a CAPTCHA has now had it
        // done by the person, and that is one of the three outcomes decision
        // 0136 section 5 says its prose reply may report.
        self.handovers_completed = self.handovers_completed.saturating_add(1);
        self.note_progress();
        // The closing half of the hand-back. The expiry edge below has no such
        // step on purpose: nobody came, and "You answered" over a window that
        // ran out is a sentence the person would rightly dispute.
        self.note(TaskActivityKind::YouAnswered, None, 0);
        Outcome::moves(TaskState::Running, StateReason::HandoverCompleted)
            .with_events(vec![TaskEvent::record(EventKind::HandoverCompleted, kind)
                .about(EventSubject::ActorLease(completion.resumed_with().clone()))])
    }

    /// The window closed with nobody coming back.
    ///
    /// The task is held, not resumed. Authority was already revoked when the
    /// handover opened, and it is revoked again here for the reason every
    /// settlement revokes: `PAUSING` means the assistant is giving its
    /// authority back, and a settlement that assumed a previous revocation
    /// still stood would be trusting a fact it did not check.
    ///
    /// The reason stays [`RevocationReason::UserTookOver`]. Nobody took the
    /// page back from the person when the timer ran out — they still have it,
    /// and the assistant is the one standing down.
    pub(super) fn on_expire_handover(
        &mut self,
        handover_id: &HandoverId,
        kind: CommandKind,
    ) -> Outcome {
        self.pending_handover = None;
        Outcome::moves(TaskState::Pausing, StateReason::HandoverExpired)
            .with_events(vec![TaskEvent::record(EventKind::HandoverExpired, kind)
                .about(EventSubject::Handover(handover_id.clone()))])
            .with_effects(settle(
                RevocationReason::UserTookOver,
                CommandKind::PauseSettled,
            ))
    }

    /// A correction or retry sent the task back to work.
    pub(super) fn on_resume_for_correction() -> Outcome {
        Outcome::moves(TaskState::Running, StateReason::CorrectionRequested)
    }

    /// The person asked a follow-up of a finished task (decision 0137).
    ///
    /// The result candidate is cleared and the recorded turn is dropped, so
    /// the agent table's first row for a running task asks the model again
    /// rather than re-offering the reply that finished the last question.
    /// Scope, consented sources, the refusal ledger and the budgets all stay:
    /// a follow-up spends the same task's budget, and exhaustion is the
    /// existing `BudgetExhausted`. The question itself is staged by the
    /// runtime and never reaches this record.
    pub(super) fn on_follow_up(&mut self, kind: CommandKind) -> Outcome {
        self.task.terminal_result = None;
        self.task.terminal_failure = None;
        self.task.consent_stage = None;
        self.pending_action = None;
        self.turn = None;
        self.unproductive_replies = 0;
        self.turns_attempting_nothing = 0;
        self.unanswered_value_asks = 0;
        self.progress = super::progress::ProgressRun::new();
        Outcome::moves(TaskState::Running, StateReason::FollowUpAsked)
            .with_events(vec![TaskEvent::record(EventKind::FollowUpAsked, kind)])
    }

    /// Whether a hold command has nothing left to do.
    fn is_settling_or_held(&self) -> bool {
        self.task.state.is_settling() || self.task.state == TaskState::Paused
    }
}
