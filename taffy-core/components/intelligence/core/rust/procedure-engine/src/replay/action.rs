// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one step of a replayed procedure does next.
//!
//! Split from [`super`] along the seam the file discipline names: that module
//! decides *where in the sequence* the task is, and this one decides what the
//! step it landed on does now — advance, wait, fail, hand over, ask, or
//! propose. The two questions have different inputs (a plan versus an action
//! ledger) and different failure modes, and keeping them apart is what stops a
//! change to the retry rule reading like a change to the sequence.
//!
//! # Nothing here reads the record for an authority fact
//!
//! The action class and the idempotency class come from
//! `task_engine::tool::ToolEntry` and never from the stored step, exactly as
//! they do on the agent path. `Guard::ToolAvailable` re-derives both when the
//! command is applied and refuses a proposal that disagrees, so a stored
//! record cannot widen anything even by being wrong — but the reason it cannot
//! is that it is never asked, and this is the file where that is either true
//! or false.
//!
//! # Three questions in one fixed order, and the order is the argument
//!
//! Once the ledger says this step has not run yet, three things are asked
//! before anything is proposed, and each of them can answer instead of
//! proposing:
//!
//! 1. **Does the page agree the step is about what the record says it is?**
//!    Rule 6, [`crate::field::disposition`]. A disagreement hands the page to
//!    the person and stops. It is first because the alternative is
//!    interrupting somebody for a value and *then* handing them the page,
//!    which is two interruptions to get one answer they could have given by
//!    looking at the field.
//! 2. **Does the step need a value only the person can give?** If so and they
//!    have not given it, the command is the ask — not a proposal of this
//!    step's verb, which cannot run without what the ask is for.
//! 3. **Is this a verb a plan step can propose at all?** Everything else
//!    refuses by name.
//!
//! # Why the ask is a command and not a proposal
//!
//! `user.request_values` is `ToolDispatch::Person`: it reaches no page, spends
//! no capability and takes no lease. The agent path turns exactly this row
//! into `Command::RequestUserInput` and this path turns it into the same
//! command, so the two agree about the one thing that is not a proposal in the
//! same words. What the person is shown is composed from a trusted local
//! template on both paths; neither holds any text to compose it from.

mod handover;
mod intent;
use handover::hand_over;
mod refresh;
pub(super) use refresh::{observe as refresh_final, poll_downloads};

use bip_types::identity::{ContentDigest, DigestAlgorithm};
use task_engine::action::{ActionProposal, ActionRecord, ActionState};
use task_engine::plan::{PlanStep, StepState};
use task_engine::proposal::{hex_digest, plan_action_material, plan_step_key};
use task_engine::task::{ConsentedSource, FailureReason};
use task_engine::tool::{ToolDispatch, ToolEntry, ToolLookup};
use task_engine::{Clock, Command, CommandKind, IdSource, Reducer, WorkflowDigest};

use super::{advance, ReplayPage, ReplayRefusal};
use crate::field::{disposition, ObservedFields, StepDisposition};
use crate::narrowing::REQUEST_VALUES_VERB;
use crate::record::Procedure;
use crate::step::{declared_type, supply_of, ArgumentSupply, ProcedureStep};

/// One step of the record, and where in the record it is.
///
/// The position travels with the step because two of the three questions above
/// are about *this* step among the ones before it: which classification is
/// about it, and how many asks the person owes answers to by the time it runs.
#[derive(Clone, Copy, Debug)]
pub(super) struct StepAt<'a> {
    /// The step.
    pub step: &'a ProcedureStep,
    /// Its position in the procedure's own step list.
    pub index: usize,
}

#[allow(clippy::too_many_arguments)]
pub(super) fn next_action_command<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    at: StepAt<'_>,
    plan_step: &PlanStep,
    source: &ConsentedSource,
    observed: ObservedFields<'_>,
    page: Option<ReplayPage<'_>>,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    let entry = entry_for(reducer, &at.step.verb)?;
    if at.step.verb == "user.handover" {
        return handover::explicit_handover(reducer, procedure, plan_step);
    }
    let actions: Vec<&ActionRecord> = reducer
        .actions()
        .filter(|action| {
            action.proposal().plan_step_id.as_ref() == Some(plan_step.plan_step_id())
                && action.proposal().tool_name() == at.step.verb
        })
        .collect();
    if actions.iter().any(|action| {
        action.state() == ActionState::Verified
            && (!matches!(
                at.step.verb.as_str(),
                "browser.dom.read"
                    | "browser.dom.query"
                    | "page.pdf.inspect"
                    | "page.screenshot.inspect"
            ) || action.observation().is_some())
    }) {
        return Ok(Some(advance(plan_step, StepState::Succeeded)));
    }
    if actions.iter().any(|action| {
        matches!(
            action.state(),
            ActionState::Proposed
                | ActionState::WaitingApproval
                | ActionState::Authorized
                | ActionState::Dispatching
                | ActionState::Verifying
        )
    }) {
        return Ok(None);
    }
    let refused = actions.iter().any(|action| {
        matches!(
            action.state(),
            ActionState::Rejected | ActionState::Cancelled
        )
    });
    if refused || actions.len() >= attempt_ceiling(entry) {
        return Ok(Some(Command::FailTask {
            reason: FailureReason::SourcesUnavailable,
        }));
    }
    if actions.iter().any(|action| {
        !matches!(
            action.state(),
            ActionState::Failed | ActionState::OutcomeUnknown
        )
    }) {
        return Err(ReplayRefusal::UnexpectedActionState);
    }
    // A recorded list also binds its browser session from the current page.
    // The runtime retires that page when the preceding learned step succeeds.
    if at.step.verb == "browser.download.list"
        || at
            .step
            .arguments
            .iter()
            .any(|argument| matches!(argument.value, crate::StepValue::SemanticTarget { .. }))
    {
        match page {
            None => return refresh::observe(reducer, procedure, plan_step, source, digest),
            Some(page)
                if !refresh::page_is_committed(reducer, procedure, plan_step, source, page) =>
            {
                return Ok(None)
            }
            Some(_) => {}
        }
    }
    // Rule 6, before anything else this step could do. A record is a claim
    // about a page it saw before, and a field it cannot classify — or one that
    // is now something else — is the page disagreeing with that claim.
    if let StepDisposition::HandToUser(reason) = disposition(at.step, observed.at(at.index)) {
        return hand_over(procedure, plan_step, reason.label()).map(Some);
    }
    // The ask, and only then the step. A fill whose value is a position in
    // what the person supplies has nothing to name until they have supplied
    // it, so proposing it first would be proposing a step that cannot run.
    let entry = if needs_the_ask(reducer, procedure, at) {
        entry_for(reducer, REQUEST_VALUES_VERB)?
    } else {
        entry
    };
    propose(
        reducer,
        procedure,
        at.step,
        plan_step,
        source,
        entry,
        actions.len(),
        page,
        digest,
    )
}

/// The registry row a verb names, at this task's milestone.
fn entry_for<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    verb: &str,
) -> Result<&'static ToolEntry, ReplayRefusal> {
    match task_engine::tool::resolve(verb, reducer.task().snapshot().milestone) {
        ToolLookup::Available(entry) => Ok(entry),
        ToolLookup::Unknown | ToolLookup::Excluded(_) | ToolLookup::Unavailable { .. } => {
            Err(ReplayRefusal::UnregisteredVerb)
        }
    }
}

/// How many times one step may be attempted before the task fails.
///
/// Read from the tool table rather than written down here, because "may this
/// be tried again without asking anybody" is already a compiled-in fact about
/// the verb. A pure read gets the one safe retry that lets a task survive
/// process death mid-dispatch; anything else gets a single attempt, because
/// `RecoveryRule` says a second one is not the runtime's to decide.
fn attempt_ceiling(entry: &ToolEntry) -> usize {
    if entry.idempotency.recovery_rule().permits_unattended_retry() {
        2
    } else {
        1
    }
}

/// Whether this step is waiting on a value the person has not supplied yet.
///
/// Derived from the task's own journal rather than held anywhere, the way
/// every other fact this module reads is: the number of asks the record owes
/// by the time this step runs, against the number of answers the person has
/// given. A replayed task has no agent turn, so those commands are this
/// module's own — and an ask that is outstanding leaves the task in
/// `WAITING_USER`, where [`super::next_procedure_command`] answers `Ok(None)`
/// and never reaches here. So the two numbers are equal exactly when every ask
/// so far has been answered, which is what makes this a comparison rather than
/// a flag somebody has to clear.
fn needs_the_ask<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    at: StepAt<'_>,
) -> bool {
    let milestone = reducer.task().snapshot().milestone;
    if !at.step.asks_for_a_supplied_value(milestone) {
        return false;
    }
    let owed = procedure
        .steps
        .iter()
        .take(at.index.saturating_add(1))
        .filter(|step| step.asks_for_a_supplied_value(milestone))
        .count();
    let counts = reducer.journal().command_counts();
    let answered = [CommandKind::SupplyUserInput, CommandKind::SupplyFieldValues]
        .iter()
        .filter_map(|kind| counts.get(kind))
        .fold(0_usize, |total, count| total.saturating_add(*count));
    answered < owed
}

#[allow(clippy::too_many_arguments)]
fn propose<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    step: &ProcedureStep,
    plan_step: &PlanStep,
    source: &ConsentedSource,
    entry: &'static ToolEntry,
    attempts: usize,
    page: Option<ReplayPage<'_>>,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    let ToolDispatch::BrowserAction(_) = entry.dispatch else {
        // The one row that reaches the person and is still something a step
        // may do. It spends no capability and takes no lease, so it is the
        // reducer's own waiting state rather than a proposal — the same
        // command `task_engine::agent` turns this row into.
        if entry.dispatch == ToolDispatch::Person && entry.name == REQUEST_VALUES_VERB {
            return Ok(Some(Command::RequestUserInput));
        }
        return Err(ReplayRefusal::UnservedVerb);
    };
    check_arguments(step, entry)?;
    let intent = intent::for_step(step, &source.tab_id, page)?;
    if !intent.is_proposable()
        || entry.canonical_name(&step.verb).as_deref() != Some(intent.tool_name())
        || entry.dispatch.action_class() != Some(intent.action_class())
        || entry.idempotency != intent.idempotency()
    {
        return Err(ReplayRefusal::UnservedVerb);
    }
    proposal_command(
        reducer,
        procedure.id.as_str(),
        plan_step,
        source,
        attempts.saturating_add(1),
        intent,
        true,
        digest,
    )
}

#[allow(clippy::too_many_arguments)]
pub(super) fn proposal_command<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    namespace: &str,
    plan_step: &PlanStep,
    source: &ConsentedSource,
    attempt: usize,
    intent: task_engine::action::ActionIntent,
    belongs_to_step: bool,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    let step_id = plan_step.plan_step_id();
    let key = plan_step_key(namespace, source, step_id, attempt);
    let material = plan_action_material(
        namespace,
        reducer.task().task_id().as_str(),
        source,
        step_id,
        &key,
        &intent,
    )
    .ok_or(ReplayRefusal::ProposalEncodingOverflow)?;
    let proposal_digest = ContentDigest {
        algorithm: DigestAlgorithm::Sha256,
        value: hex_digest(
            &digest
                .sha256(&material)
                .map_err(|_| ReplayRefusal::DigestUnavailable)?,
        ),
    };
    Ok(Some(Command::ProposeAction(Box::new(ActionProposal::new(
        intent,
        belongs_to_step.then(|| step_id.clone()),
        key,
        // This replay advances a plan step only on a verified outcome, so it
        // checks the evidence itself. Marking the action required would put a
        // permanently unknown first attempt into the explanatory plan and
        // refuse the one safe retry after process death.
        false,
        None,
        proposal_digest,
    )))))
}

/// Whether every argument of `step` is one this path can stand behind.
///
/// Classified by what each argument needs *produced*, not by whether it is a
/// literal. The two are not the same question and the difference is the whole
/// of what this path serves: nothing here binds a handle — the table that
/// would answer is a model turn's, a replay has none, and reaching for the tab
/// instead is the repair that reaches a node nobody chose — while a position
/// in what the person supplies needs no binding at all, because the person is
/// asked again and the browser mints the value out of the answer.
///
/// The scan is in the record's own argument order, so a step with two faults
/// is refused for the same one every time it is looked at.
fn check_arguments(step: &ProcedureStep, entry: &ToolEntry) -> Result<(), ReplayRefusal> {
    for argument in &step.arguments {
        match supply_of(&argument.value, declared_type(entry, &argument.name)) {
            ArgumentSupply::Recorded
            | ArgumentSupply::PersonsPosition
            | ArgumentSupply::SemanticTarget => {}
            ArgumentSupply::Handle => return Err(ReplayRefusal::HandleBindingUnavailable),
            ArgumentSupply::PersonsText => return Err(ReplayRefusal::PersonValueIsNotAPosition),
        }
    }
    Ok(())
}
