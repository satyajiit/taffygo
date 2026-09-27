// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The action lifecycle: proposal, policy decision, dispatch, outcome.
//!
//! This is the module that touches the authority boundary most often, and it
//! is the module with the least authority. Nothing here authorizes anything:
//! a proposal leaves as [`crate::effect::Effect::AskPolicy`], the decision
//! arrives back as a command, and a dispatch is refused unless the record
//! already says the action was authorized.
//!
//! Two facts are recorded here rather than recomputed later: the idempotency
//! key of every dispatched attempt, so a replay cannot repeat it, and an
//! attempt whose outcome never arrived, which becomes a reconciliation and
//! never a retry.
//!
//! [`Reducer::on_propose_action`] is also the only insert into the action
//! register, which is why the register's ceiling
//! ([`crate::MAX_ACTIONS_PER_TASK`]) is checked here and nowhere else.

use bip_types::identity::{ActionId, DispatchId};

use super::activity::refusal_step;
use super::outcome::Outcome;
use super::Reducer;
use crate::action::{
    ActionIntent, ActionOutcome, ActionProposal, ActionRecord, ActionState, BrowserIntent,
};
use crate::authority::{ActionClass, ProposalDecision};
use crate::budget::BudgetKind;
use crate::command::CommandKind;
use crate::deps::TabId;
use crate::effect::Effect;
use crate::event::{EventKind, EventSubject, TaskEvent};
use crate::ids::{next_action_id, IdSource};
use crate::task::{host_of_origin, ConsentStage, StateReason, TaskActivityKind, TaskState};
use crate::time::Clock;
use crate::transition::RefusalReason;

mod discovery;
mod tool_job;

use discovery::DiscoveredSourceChange;

/// Whether this outcome belongs in the register that counts the model's calls.
///
/// Only a terminal refusal does. `Verified` is not a refusal, and the two
/// non-terminal codes — an approval the task is parked on, and a navigation
/// still being verified — are answers that have not arrived yet; counting
/// either would abandon a task for waiting.
///
/// And only a refusal of the model's own call. The walk's whole-document read
/// is proposed by this crate, and it fingerprints as `(browser.dom.read, tab,
/// no node)` — the same number a model's read of the same tab produces. So the
/// two authors shared one counter: the walk's reads spent the model's
/// allowance and were spent by it. The walk's bound is
/// [`crate::MAX_SOURCE_BOOTSTRAP_READS`], and it is kept where its facts are
/// (decision 0196).
///
/// The task's own fill of a value the person supplied is the same case: it is
/// made once per position and never retried, and a refusal of it is not the
/// model repeating anything (decision 0238). So is its scroll of a challenge
/// into view before an ask, made at most once per ask (decision 0240).
fn counts_against_the_model(proposal: &ActionProposal, code: bip_types::ActionResultCode) -> bool {
    code.fails_closed() && code.is_terminal() && !crate::agent::is_task_move(proposal)
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// The runtime needs the user's approval for an action it already holds.
    pub(super) fn on_request_approval(
        &mut self,
        action_id: &ActionId,
        kind: CommandKind,
    ) -> Outcome {
        self.task.consent_stage = Some(ConsentStage::InTask);
        self.pending_action = Some(action_id.clone());
        // An approval is a wait on the person, so it is a step like every other
        // wait — and this one knows its page, because the action it is about
        // names the tab.
        let host = self
            .actions
            .get(action_id.as_str())
            .map(|action| action.proposal().tab_id().clone())
            .and_then(|tab| self.host_of_tab(&tab));
        self.note(TaskActivityKind::AskedYou, host, 0);
        Outcome::moves(
            TaskState::AwaitingConsent,
            StateReason::ScopeExpansionNeedsConsent,
        )
        .with_events(vec![TaskEvent::record(EventKind::ApprovalRequested, kind)
            .about(EventSubject::Action(action_id.clone()))])
        .with_effects(vec![Effect::RequestApproval {
            action_id: action_id.clone(),
        }])
    }

    /// Records a proposal and asks policy about it. The budget is charged
    /// here because the proposal is what spends it; the guard that admits the
    /// draw has already run.
    pub(super) fn on_propose_action(
        &mut self,
        proposal: &ActionProposal,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        // The one place the action register grows, and therefore the one place
        // it can be bounded. The check precedes minting, so a refused proposal
        // spends no identifier and a replay of the same journal mints the same
        // sequence it did the first time.
        if self.actions.len() >= super::MAX_ACTIONS_PER_TASK {
            return Err(RefusalReason::ActionRegisterFull);
        }
        let action_id = next_action_id(&mut self.ids).ok_or(RefusalReason::IdSourceExhausted)?;
        let mut events = vec![TaskEvent::record(EventKind::ActionProposed, kind)
            .about(EventSubject::Action(action_id.clone()))];
        if let Some(draw) = proposal.budget_draw {
            self.task.ledger.charge(draw.kind, draw.amount);
            events.push(TaskEvent::record(EventKind::BudgetCharged, kind));
        }
        if let (Some(plan), Some(step)) = (self.plan.as_mut(), proposal.plan_step_id.as_ref()) {
            plan.attach_action(step, &action_id, proposal.required_for_step);
        }
        self.actions.insert(
            action_id.as_str().to_owned(),
            ActionRecord::proposed(action_id.clone(), proposal.clone()),
        );
        Ok(Outcome::recorded(
            events,
            vec![Effect::AskPolicy { action_id }],
        ))
    }

    /// Applies the decision `policy-engine` made about a proposal.
    pub(super) fn on_record_policy_decision(
        &mut self,
        action_id: &ActionId,
        decision: &ProposalDecision,
        dispatch_id: Option<&DispatchId>,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        // A policy reply may race a pause, cancellation, or another wait. Only
        // RUNNING is allowed to accept fresh authority; every later reply is
        // still journalled, but it closes the action instead of reviving work.
        let authority_is_closed = self.task.state != TaskState::Running;
        let Some(action) = self.actions.get_mut(action_id.as_str()) else {
            return Err(RefusalReason::UnknownAction);
        };
        action.apply_policy_decision(decision, authority_is_closed);
        let mut state = action.state();
        if state == ActionState::Authorized && dispatch_id.is_some() {
            let Some(dispatch_id) = dispatch_id else {
                return Err(RefusalReason::ActionNotAuthorized);
            };
            if !action.begin_dispatch(dispatch_id.clone()) {
                return Err(RefusalReason::ActionNotAuthorized);
            }
            state = action.state();
        } else if dispatch_id.is_some() {
            return Err(RefusalReason::ActionNotAuthorized);
        }
        if state == ActionState::WaitingApproval {
            return Ok(self.on_request_approval(action_id, kind));
        }
        let capability = action.capability_id().cloned();
        // Exhaustive: this value becomes an audit event, and an arm that
        // swept up a state added later would record "rejected" for an action
        // that was not rejected. The three outcomes a policy decision can
        // produce are named; every other state is unreachable from here and
        // says so by mapping to the same refusal it would already have been.
        let event_kind = match state {
            ActionState::Authorized | ActionState::Dispatching => EventKind::CapabilityIssued,
            ActionState::WaitingApproval => EventKind::ApprovalRequested,
            ActionState::Rejected
            | ActionState::Proposed
            | ActionState::Verifying
            | ActionState::Verified
            | ActionState::Failed
            | ActionState::Cancelled
            | ActionState::OutcomeUnknown => EventKind::ActionRejected,
        };
        let subject = capability.map_or_else(
            || EventSubject::Action(action_id.clone()),
            EventSubject::Capability,
        );
        let effects = match state {
            ActionState::WaitingApproval => vec![Effect::RequestApproval {
                action_id: action_id.clone(),
            }],
            ActionState::Dispatching => {
                let key = action.proposal().idempotency_key.clone();
                let effect = dispatch_effect(self.task.task_id(), action_id, action.proposal());
                self.dispatched_keys.insert(key);
                vec![effect]
            }
            ActionState::Proposed
            | ActionState::Authorized
            | ActionState::Verifying
            | ActionState::Verified
            | ActionState::Failed
            | ActionState::Rejected
            | ActionState::Cancelled
            | ActionState::OutcomeUnknown => Vec::new(),
        };
        if state == ActionState::Rejected {
            self.last_move_refused = true;
            if let ProposalDecision::Deny(denial) = decision {
                self.note_refused_by_policy(action_id, denial.code);
            }
        }
        let mut events = vec![TaskEvent::record(event_kind, kind).about(subject)];
        if state == ActionState::Dispatching {
            events.push(
                TaskEvent::record(EventKind::ActionDispatchStarted, kind)
                    .about(EventSubject::Action(action_id.clone())),
            );
        }
        Ok(Outcome::recorded(events, effects))
    }

    /// Counts a policy refusal of the model's call in the repetition register.
    ///
    /// Decision 0054 section 5 counts repeats of `(tool, target, refusal code)`
    /// and its ladder is written for a policy denial by name — "a policy
    /// denial seen a second time does not become a thing worth observing the
    /// page about" — yet only a refusal the *browser* answered ever reached the
    /// register, because the one call that records sits in the outcome path
    /// and a denied proposal never gets an outcome. So a call policy refused
    /// could be proposed and refused for as long as the budget lasted, while a
    /// call the browser refused the same way was abandoned at the third. Now
    /// both are the same count, under the same rule about who is counted
    /// (decision 0233).
    fn note_refused_by_policy(&mut self, action_id: &ActionId, code: bip_types::ActionResultCode) {
        let Some(action) = self.actions.get(action_id.as_str()) else {
            return;
        };
        if counts_against_the_model(action.proposal(), code) {
            let call = Self::call_of(action.proposal());
            self.refusals.record(call, code);
        }
    }

    /// Begins one dispatch attempt and remembers its idempotency key.
    pub(super) fn on_dispatch_action(
        &mut self,
        action_id: &ActionId,
        dispatch_id: &DispatchId,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        let Some(action) = self.actions.get_mut(action_id.as_str()) else {
            return Err(RefusalReason::UnknownAction);
        };
        if !action.begin_dispatch(dispatch_id.clone()) {
            return Err(RefusalReason::ActionNotAuthorized);
        }
        let key = action.proposal().idempotency_key.clone();
        let effect = dispatch_effect(self.task.task_id(), action_id, action.proposal());
        self.dispatched_keys.insert(key);
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::ActionDispatchStarted, kind)
                .about(EventSubject::Action(action_id.clone()))],
            vec![effect],
        ))
    }

    /// Records what an attempt did. An attempt with no terminal answer is
    /// reconciled under its recovery rule, never retried blind.
    pub(super) fn on_record_action_outcome(
        &mut self,
        action_id: &ActionId,
        outcome: &ActionOutcome,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        if !self.action_outcome_matches(action_id, outcome) {
            return Err(RefusalReason::ActionOutcomeMismatch);
        }
        let source_change = self.discovered_source_change(action_id, outcome)?;
        // A terminal or settling task may still need to record the one exact
        // result of work that was already dispatched. It must not turn that
        // late result into a new browser effect after authority was revoked.
        let may_reconcile = !self.task.state.is_terminal() && !self.task.state.is_settling();
        let Some(action) = self.actions.get_mut(action_id.as_str()) else {
            return Err(RefusalReason::UnknownAction);
        };
        action.record_outcome(outcome);
        let action_class = action.proposal().action_class();
        let reached_a_page = action.proposal().destination_address().is_some();
        let tab = action.proposal().tab_id().clone();
        let call = Self::call_of(action.proposal());
        let landed_the_tab_somewhere_new = action.state() == ActionState::Verified
            && crate::agent::lands_the_tab_somewhere_new(action.proposal().intent());
        if counts_against_the_model(action.proposal(), outcome.code) {
            self.refusals.record(call, outcome.code);
            self.last_move_refused = true;
        }
        if action.state() == ActionState::Verified {
            self.last_move_refused = false;
        }
        // Where this move landed, read before the verdict is consumed below:
        // `Some(true)` is an arrival at a site the task already held.
        //
        // The third arm is the one the bound was written for and did not have.
        // `MAX_FRUITLESS_ERRAND_ARRIVALS` says in its own doc that it exists
        // because "a `browser.search` that succeeds and lands on the same
        // results page moves no refusal counter" — and that search produces
        // `DiscoveredSourceChange::None`, not `AlreadyBound`, because the
        // browser offers a source only for a tab that moved off the origin its
        // source names. So the counter never saw the loop it was built for:
        // search, read, query, search again, on one results page.
        //
        // Only a search. A navigate or a link open that stays on one origin is
        // ordinary progress through a site — a phone's errand legitimately
        // walks `myaadhaar.uidai.gov.in` to the page it needs — and counting
        // those would end a run that is working. A search is the one move that
        // is never an end in itself: leaving the results page admits a source
        // and resets this to zero (decision 0167).
        let repeated_search = action.state() == ActionState::Verified
            && matches!(source_change, DiscoveredSourceChange::None)
            && matches!(
                action.proposal().intent(),
                ActionIntent::Browser(BrowserIntent::Search { .. })
            );
        let arrival = match (action.state(), &source_change) {
            (ActionState::Verified, DiscoveredSourceChange::AlreadyBound) => Some(true),
            (ActionState::Verified, DiscoveredSourceChange::Admit { .. }) => Some(false),
            _ if repeated_search => Some(true),
            _ => None,
        };
        let effects = if may_reconcile {
            reconciliation_of(action_id, action)
        } else {
            Vec::new()
        };
        // Before the move supersedes anything: whether this action changed
        // anything is a comparison with the records its tab already held, and
        // a move that marked them first would find every repeat new
        // (decision 0233).
        self.note_action_settled(action_id, arrival);
        if landed_the_tab_somewhere_new {
            self.supersede_settled_records_of(&tab, action_id);
        }
        let mut events = vec![
            TaskEvent::record(EventKind::ActionVerificationCompleted, kind)
                .about(EventSubject::Action(action_id.clone())),
        ];
        if let DiscoveredSourceChange::Admit {
            source,
            replaced_source_id,
        } = source_change
        {
            if let Some(replaced) = replaced_source_id {
                self.task.scope.exclude(&replaced);
                self.task
                    .consented_sources
                    .retain(|held| held.source_id != replaced);
            }
            self.task.scope = self.task.scope.clone().include(source.source_id);
            self.task.consented_sources.push(source.clone());
            self.task
                .consented_sources
                .sort_unstable_by_key(|held| held.source_id);
            self.task.ledger.charge(BudgetKind::MaxSources, 1);
            self.task.snapshot.remaining_new_source_cap = self
                .task
                .snapshot
                .remaining_new_source_cap
                .saturating_sub(1);
            events.push(
                TaskEvent::record(EventKind::SourceScopeSet, kind)
                    .about(EventSubject::Source(source.source_id)),
            );
            events.push(TaskEvent::record(EventKind::BudgetCharged, kind));
        }
        if let Some(already_bound) = arrival {
            self.note_arrival(already_bound);
        }
        // What the task did, in order (decision 0148). Here rather than at the
        // dispatch, because a dispatch is a request and this is the answer: an
        // action that was never answered is not a step, and one answered twice
        // cannot be, since a second outcome for the same identifier is refused
        // above.
        //
        // A verified move is a step only when it reached a page — a click that
        // pressed a button on a page already open is not "Opened", and saying
        // so would fill the record with moves nobody asked to read about. The
        // page-reaching ones are exactly the navigations, which is what the
        // discovered source admitted above also comes from.
        if outcome.code.is_terminal() {
            // The browser's own committed origin where there is one, because
            // it is the page that was actually reached; the task's consented
            // source for the tab otherwise — which is why this is written
            // after the admission above rather than before it, since the move
            // that discovers a source is exactly the move whose step wants to
            // name it. Never the proposal's destination: that is where the
            // move was aimed, and a refusal is exactly the case where it did
            // not land there.
            let host = outcome
                .observation
                .as_ref()
                .and_then(|seen| host_of_origin(&seen.normalized_origin))
                .or_else(|| self.host_of_tab(&tab));
            match () {
                () if outcome.code.fails_closed() => {
                    self.note(refusal_step(outcome.code), host, 0);
                }
                // A read is the observation, which is the only action that
                // brings a page's meaning back. Its count is nought and stays
                // nought: what a read *found* is a number of facts, and facts
                // are the workspace's, not the reducer's — the surface joins
                // the host to its own source list for that (decision 0148).
                () if action_class == ActionClass::ObservePage => {
                    self.note(TaskActivityKind::ReadPage, host, 0);
                }
                // A verified move that named a destination reached a page. A
                // click on a page already open did not, and a record full of
                // "Opened" for every button press is a record nobody reads.
                () if reached_a_page => self.note(TaskActivityKind::OpenedPage, host, 0),
                () => {}
            }
        }
        Ok(Outcome::recorded(events, effects))
    }

    /// Marks every settled record of `tab` as being about a page that is no
    /// longer there, because a move of it has just verified.
    ///
    /// This is the only moment that fact is knowable in order. `self.actions`
    /// is a map keyed by identifier — `act_10` sorts before `act_2` — so past
    /// ten actions no later reader can tell which of two records came first,
    /// and the one reader that needs to know is the pre-model bootstrap read's
    /// bound, whose subject is a document and which was counting a tab
    /// (decision 0181).
    ///
    /// Only settled records are marked. An attempt still in flight settles
    /// under the move it raced, and the walk must go on seeing it as pending
    /// rather than propose a second reading beside it.
    fn supersede_settled_records_of(&mut self, tab: &TabId, mover: &ActionId) {
        for other in self.actions.values_mut() {
            if other.action_id != *mover
                && other.state.is_terminal()
                && other.proposal.tab_id() == tab
            {
                other.superseded_by_a_move = true;
            }
        }
    }

    pub(super) fn action_outcome_matches(
        &self,
        action_id: &ActionId,
        outcome: &ActionOutcome,
    ) -> bool {
        let Some(action) = self.actions.get(action_id.as_str()) else {
            return false;
        };
        if !action.accepts_outcome(outcome) {
            return false;
        }
        // The tab is the match, and the source it names must be in scope.
        //
        // This used to require `source.normalized_origin` to equal the
        // evidence's origin byte for byte, and that was a third copy of a rule
        // the browser owns. A site may answer on a sibling host of its own
        // registrable domain, and a page may move there with a client-side
        // route after the commit; deciding whether that is still the same site
        // needs the registry-controlled domain table, which a sandboxed
        // utility has no way to acquire. The browser holds it and has already
        // spent it: no observation reaches here that
        // `AcceptedApprovalLedger::IsTaskSourceAuthorized` did not admit
        // against this exact source. Re-deciding it here with a weaker test
        // could only ever refuse what the browser allowed — which is what
        // happened: a press on `myaadhaar.uidai.gov.in` that landed on
        // `tathya.uidai.gov.in` made the next read's evidence match no source,
        // and the completion was refused as `ActionOutcomeMismatch`, ending
        // the core and every task in the profile with it.
        let observation_matches = outcome.observation.as_ref().is_none_or(|evidence| {
            self.task.consented_sources.iter().any(|source| {
                source.tab_id == evidence.tab_id
                    && self.task.scope.included().contains(&source.source_id)
            })
        });
        observation_matches && self.discovered_source_change(action_id, outcome).is_ok()
    }
}

/// What recovery asks of the browser for an action whose outcome is unknown.
///
/// A read has no side effect to reconcile. Asking the browser whether one
/// landed can only come back "unknown" again, and the bridge answers an unknown
/// with `RequestUserInput`, so a read whose answer was lost handed the whole
/// errand to the person. Its rule is the one that permits an unattended retry:
/// the model is told the read did not come back and reads again (decision
/// 0234).
fn reconciliation_of(action_id: &ActionId, action: &ActionRecord) -> Vec<Effect> {
    if awaits_reconciliation(action) {
        vec![Effect::ReconcileAction {
            action_id: action_id.clone(),
            rule: action.recovery_rule(),
        }]
    } else {
        Vec::new()
    }
}

/// Whether an action's unknown outcome is still an open question.
///
/// One answer for the two places that ask it: whether recovery reconciles
/// the action, and whether the task may end while it stands. A read and the
/// task's own scroll before an ask are never reconciled — the read has no
/// side effect (decision 0234), and whatever the scroll did, the ask it was
/// made for goes out next (decisions 0240 and 0245). Nothing will ever settle
/// them, so they cannot hold the task's ending back either: a task whose
/// ending the walk plans and the reducer refuses takes the core with it
/// (decision 0248).
pub(super) fn awaits_reconciliation(action: &ActionRecord) -> bool {
    action.state() == ActionState::OutcomeUnknown
        && !action.recovery_rule().permits_unattended_retry()
        && !crate::agent::is_ask_view(action.proposal())
}

/// The effect one authorized dispatch becomes.
///
/// A browser action goes to the browser broker; a tool job goes to the tool
/// broker under the identity derived from the task and the action. The
/// runtime family is derived from the canonical intent; the guard independently
/// checks that the exact registry row declares the same dispatch.
fn dispatch_effect(
    task_id: &bip_types::identity::TaskId,
    action_id: &ActionId,
    proposal: &crate::action::ActionProposal,
) -> Effect {
    if matches!(proposal.intent(), crate::action::ActionIntent::Library(_)) {
        return Effect::RunLibraryTool {
            action_id: action_id.clone(),
        };
    }
    if matches!(proposal.intent(), crate::action::ActionIntent::Memory(_)) {
        return Effect::RunMemoryTool {
            action_id: action_id.clone(),
        };
    }
    if let Some(runtime) = proposal.tool_runtime() {
        return Effect::RunToolJob {
            action_id: action_id.clone(),
            job_id: crate::tool::job_id_for_action(task_id, action_id),
            runtime,
        };
    }
    Effect::DispatchAction {
        action_id: action_id.clone(),
    }
}
