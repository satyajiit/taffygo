// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Proposing the next command from a procedure that has already matched.
//!
//! # Replay is a fast path and never a trusted one
//!
//! Decision 0055 section 7. A matched procedure changes which command is
//! *proposed* next and changes nothing about what happens to that command
//! afterwards: every step is still a proposal from `task-engine`, decided by
//! `policy-engine`, minted as a capability, dispatched under a lease, verified
//! by its postcondition and recorded by `audit-engine`. The saving is the
//! model call, which is most of the cost. The saving is emphatically not the
//! checks — and this module is where that claim is either true or false, so it
//! is worth saying what it does *not* do: it never dispatches, never
//! authorises, never widens a scope, and never reaches a page. It returns a
//! [`Command`] the caller must commit through the same reducer every other
//! command goes through.
//!
//! # Why this file exists at all, rather than a second workflow
//!
//! `task_engine::workflow` runs the reviewed local `BuildSourceTable` sequence
//! from hard-coded logic. This runs the same sequence from a stored record.
//! The conformance oracle (`tests/procedure_matches_hardcoded_workflow.rs`)
//! drives one task through both and asserts an identical `Command` sequence,
//! which is what lets the hard-coded half be deleted afterwards instead of
//! kept beside a second implementation of itself.
//!
//! So the two are deliberately *not* factored together. What they share is
//! `task_engine::proposal` — how a proposal's identity is canonicalised, which
//! has to agree byte for byte or the comparison would fail for a reason
//! nobody cares about — and `task_engine::template`, which owns the prose a
//! person reads because a procedure record carries none. Everything the oracle
//! is actually about is decided independently on each side: which tool, at
//! which step, on which attempt, and whether to propose, advance, wait or
//! fail.
//!
//! # What a step is allowed to be, here and now
//!
//! A step whose argument names an earlier step's handle is refused rather than
//! resolved ([`ReplayRefusal::HandleBindingUnavailable`]). The handle table is
//! a model turn's artifact — a replayed procedure has no turn, so there is
//! nothing for the number to name. Reaching for the tab instead is the repair
//! that turns a stale record into a live action on a node nobody chose, and
//! `task_engine::handle` exists because of exactly that.
//!
//! A step whose argument is a value only the person can give is a different
//! case and is **served**, in two commands rather than one: the ask first, and
//! the step itself only once the person has answered. The record names the
//! field's purpose and never what was typed into it, so there is nothing stale
//! to bind — the position is minted at replay, in the browser, out of an
//! answer this process never sees (decisions 0063 and 0088). A person's value
//! at a parameter that takes *bytes* rather than a position is not served and
//! is not a handle either: it is
//! [`ReplayRefusal::PersonValueIsNotAPosition`], because a replay has no model
//! to read text back to and no field on a proposal to carry it — and because
//! the values these fields hold are the ones that never enter the AI data
//! plane at all.
//!
//! # Rule 6 is decided here, against the page in front of the step
//!
//! [`crate::field::disposition`] is consulted before anything else about a
//! step, and a [`crate::field::StepDisposition::HandToUser`] becomes
//! `Command::RequestHandover` rather than a proposal. It comes first for a
//! reason a reader should be able to check: a step whose field the page and
//! the record disagree about must not first interrupt the person for a value
//! and *then* hand them the page, which is two interruptions to reach one
//! answer they could have given by looking at the field.
//!
//! Production still supplies [`crate::field::ObservedFields::none`], so fills
//! are handed to the person. Saved explicit handover steps wait for their own
//! committed handback. Semantic click, focus, link-open and download targets
//! instead bind to a complete current [`ReplayPage`], with a unique role and
//! compiled phrase match and a matching committed observation receipt. A
//! missing page first proposes an ordinary read, which cannot finish the
//! operation it precedes. Downloads resolve their link in the browser and
//! finish only after the task-owned download table reports completion.
//!
//! # The narrowing is a precondition, not a decoration
//!
//! Before anything is proposed, the task's effective set is narrowed by the
//! procedure ([`crate::narrowing::narrow`]) and every verb the procedure names
//! must survive it. A verb the narrowed set does not admit is one the task was
//! never allowed to call, and proposing it anyway would leave the refusal to
//! `Guard::ToolAvailable` — which would refuse it, but one committed command
//! later, with the procedure looking like it had authority it never had.

mod action;
mod page;
mod plan;
pub use page::{ReplayNode, ReplayPage};

use policy_engine::origin::normalize_serialization;
use task_engine::action::ActionState;
use task_engine::plan::{Plan, PlanStatus, PlanStep, StepKind, StepState};
use task_engine::task::{ConsentedSource, TaskResult, TaskState};
use task_engine::tool::EffectiveToolSet;
use task_engine::{Clock, Command, IdSource, ObservationGraphSummary, Reducer, WorkflowDigest};

use self::action::{next_action_command, StepAt};
use crate::field::ObservedFields;
use crate::narrowing::{narrow, required_verbs, NarrowingRefusal};
use crate::record::Procedure;

/// Why a procedure could not truthfully say what happens next.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ReplayRefusal {
    /// The procedure is not `Active`. Being runnable is the first of several
    /// necessary conditions and none of them is sufficient.
    NotRunnable,
    /// The task did not retain exactly one accepted source in scope.
    InvalidSourceScope,
    /// The task's source is not on the origin the procedure was written for.
    ScopeDoesNotCoverTheSource,
    /// The procedure could not narrow the task's tool set at all.
    NarrowingRefused(NarrowingRefusal),
    /// A verb the narrowed set does not admit.
    VerbNotAdmitted,
    /// A verb no registry row matches at this milestone.
    UnregisteredVerb,
    /// A verb this replay interpreter cannot serve. Explicit handover and
    /// person-value requests use their own normal reducer commands.
    UnservedVerb,
    /// A prior-step handle or semantic target without a unique fresh binding.
    HandleBindingUnavailable,
    /// A person's value at a parameter that takes bytes rather than a
    /// position.
    ///
    /// Two reasons, either one sufficient. A replay has no model turn, so text
    /// the person typed for the assistant to read has nothing to be read by
    /// and no field on an `ActionProposal` to travel in. And the fields whose
    /// values only a person can give are the ones whose bytes never enter the
    /// AI data plane at all, so the shape that would carry them past this
    /// point is the one shape that must not exist.
    PersonValueIsNotAPosition,
    /// The step carries text that only a live turn residency may bind to an
    /// opaque operand reference. Procedure replay has no such residency.
    TransientOperandUnavailable,
    /// A handover identity longer than the reducer accepts.
    ///
    /// Beside [`Self::ProposalEncodingOverflow`] and for the same reason: the
    /// identity is derived rather than minted, so it can only fail by being
    /// too long, and a refusal is the answer rather than a shortened identity
    /// that would name a different handover.
    HandoverIdentityOverflow,
    /// The task's template describes no reviewed plan.
    MissingTemplatePlan,
    /// A plan with an unexpected shape replaced the procedure's.
    UnexpectedPlan,
    /// Durable action state contradicted the procedure.
    UnexpectedActionState,
    /// Result evidence was absent after the final step completed.
    MissingObservationEvidence,
    /// The local digest adapter was unavailable.
    DigestUnavailable,
    /// Bounded canonical proposal material overflowed.
    ProposalEncodingOverflow,
}

impl ReplayRefusal {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::NotRunnable => "not_runnable",
            Self::InvalidSourceScope => "invalid_source_scope",
            Self::ScopeDoesNotCoverTheSource => "scope_does_not_cover_the_source",
            Self::NarrowingRefused(reason) => reason.label(),
            Self::VerbNotAdmitted => "verb_not_admitted",
            Self::UnregisteredVerb => "unregistered_verb",
            Self::UnservedVerb => "unserved_verb",
            Self::HandleBindingUnavailable => "handle_binding_unavailable",
            Self::PersonValueIsNotAPosition => "person_value_is_not_a_position",
            Self::TransientOperandUnavailable => "transient_operand_unavailable",
            Self::HandoverIdentityOverflow => "handover_identity_overflow",
            Self::MissingTemplatePlan => "missing_template_plan",
            Self::UnexpectedPlan => "unexpected_plan",
            Self::UnexpectedActionState => "unexpected_action_state",
            Self::MissingObservationEvidence => "missing_observation_evidence",
            Self::DigestUnavailable => "digest_unavailable",
            Self::ProposalEncodingOverflow => "proposal_encoding_overflow",
        }
    }
}

/// The next replayable command `procedure` proposes for `reducer`'s task.
///
/// `Ok(None)` means the same three things it means on the reviewed path:
/// another committed effect or user decision is pending, the task is terminal,
/// or the plan is waiting on a dependency.
///
/// `observed` is what the classifier says about the field each step is about
/// **now**, which is the page half of rule 6 (see [`crate::field`]). It is a
/// parameter rather than something read here because this crate has no page:
/// the classification is made in the browser process, from the semantic graph,
/// at the moment the step is about to run.
pub fn next_procedure_command<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    observed: ObservedFields<'_>,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    next_procedure_command_with_page(reducer, procedure, observed, None, digest)
}

/// The same replay, binding semantic targets only to the supplied fresh page.
pub fn next_procedure_command_with_page<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    observed: ObservedFields<'_>,
    page: Option<ReplayPage<'_>>,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    if !procedure.is_runnable() {
        return Err(ReplayRefusal::NotRunnable);
    }
    let source = replay_source(reducer, procedure)?;
    if page.is_some_and(|page| {
        page.evidence.tab_id != source.tab_id
            || page.evidence.normalized_origin != procedure.scope.origin().display()
    }) {
        return Err(ReplayRefusal::ScopeDoesNotCoverTheSource);
    }
    match reducer.task().state() {
        TaskState::Queued => Ok(Some(Command::ExecutorStarted)),
        TaskState::Running => running_command(reducer, procedure, source, observed, page, digest),
        TaskState::Completing => result_command(reducer).map(Some),
        TaskState::Draft
        | TaskState::AwaitingConsent
        | TaskState::WaitingUser
        | TaskState::Pausing
        | TaskState::Paused
        | TaskState::Cancelling
        | TaskState::Cancelled
        | TaskState::Completed
        | TaskState::Partial
        | TaskState::Failed => Ok(None),
    }
}

/// The one source this replay acts on, once the task's configuration is one
/// the procedure applies to.
fn replay_source<'a, C: Clock, I: IdSource>(
    reducer: &'a Reducer<C, I>,
    procedure: &Procedure,
) -> Result<&'a ConsentedSource, ReplayRefusal> {
    let snapshot = reducer.task().snapshot();
    let Some(source) = snapshot.consented_sources.first() else {
        return Err(ReplayRefusal::InvalidSourceScope);
    };
    if snapshot.consented_sources.len() != 1
        || (snapshot.source_discovery_enabled
            && snapshot.template_id != task_engine::TaskTemplateId::WebErrand)
        || reducer.task().scope().included() != [source.source_id]
    {
        return Err(ReplayRefusal::InvalidSourceScope);
    }
    // Same-origin and never same-site, decided by the record's own scope. A
    // page that resembles the one a procedure was written for is not evidence
    // about the page in front of the assistant now.
    let Ok(origin) = normalize_serialization(&source.normalized_origin) else {
        return Err(ReplayRefusal::ScopeDoesNotCoverTheSource);
    };
    if !procedure.scope.covers(&origin) {
        return Err(ReplayRefusal::ScopeDoesNotCoverTheSource);
    }
    let narrowed = narrow(
        &EffectiveToolSet::for_task(snapshot.milestone, &snapshot.tool_allowlist),
        procedure,
    )
    .map_err(ReplayRefusal::NarrowingRefused)?;
    // The same list `narrow` was given, so the set a consent screen is built
    // from and the set this checks itself against cannot come apart. It is
    // deliberately not `procedure.verbs()`: a record whose fill takes the
    // person's value needs one name its steps do not spell, and a replay that
    // asked with a verb the narrowing had removed would be proposing something
    // the task was never allowed to do.
    if !required_verbs(procedure, snapshot.milestone)
        .iter()
        .all(|verb| narrowed.admits(verb.as_str()))
    {
        return Err(ReplayRefusal::VerbNotAdmitted);
    }
    Ok(source)
}

fn running_command<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
    source: &ConsentedSource,
    observed: ObservedFields<'_>,
    page: Option<ReplayPage<'_>>,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    let Some(plan) = reducer.plan() else {
        return Ok(Some(Command::SetPlan(replay_plan(reducer, procedure)?)));
    };
    if plan.status() == PlanStatus::Superseded {
        return Ok(Some(Command::SetPlan(replay_plan(reducer, procedure)?)));
    }
    if plan.steps().len() != procedure.steps.len().saturating_add(1)
        || plan.status() != PlanStatus::Active
    {
        if plan.status() == PlanStatus::Completed {
            return Ok(Some(Command::ResultCandidateReady));
        }
        return Err(ReplayRefusal::UnexpectedPlan);
    }
    check_final_step_shape(plan, procedure.steps.len())?;
    for (index, step) in procedure.steps.iter().enumerate() {
        let plan_step = plan
            .steps()
            .get(index)
            .ok_or(ReplayRefusal::UnexpectedPlan)?;
        match plan_step.state() {
            StepState::Ready => return Ok(Some(advance(plan_step, StepState::Running))),
            StepState::Running => {
                return next_action_command(
                    reducer,
                    procedure,
                    StepAt { step, index },
                    plan_step,
                    source,
                    observed,
                    page,
                    digest,
                )
            }
            StepState::Succeeded => {}
            // A step with no dependency cannot legitimately be waiting on one,
            // so `Pending` there is a contradiction rather than a pause. A step
            // that has one is simply not ready yet.
            StepState::Pending if !plan_step.dependencies().is_empty() => return Ok(None),
            StepState::Pending
            | StepState::Waiting
            | StepState::Skipped
            | StepState::Failed
            | StepState::Cancelled
            | StepState::Superseded => return Err(ReplayRefusal::UnexpectedPlan),
        }
    }
    let final_step = plan
        .steps()
        .get(procedure.steps.len())
        .ok_or(ReplayRefusal::UnexpectedPlan)?;
    next_final_command(reducer, final_step, procedure, source, page, digest)
}

/// The plan a replayed procedure works from.
///
/// The template's, not the record's: decision 0055 section 4 makes a step a
/// verb, a bounded argument record and a postcondition, so a procedure carries
/// no prose for a person to read. See `task_engine::template`.
fn replay_plan<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    procedure: &Procedure,
) -> Result<task_engine::PlanDraft, ReplayRefusal> {
    if reducer.is_web_errand() {
        return Ok(plan::errand_plan(procedure));
    }
    task_engine::template::plan_for(reducer.task().snapshot().template_id)
        .ok_or(ReplayRefusal::MissingTemplatePlan)
}

/// The last plan step turns what was observed into the result, and waits on
/// the step before it.
///
/// Checked rather than assumed, because a plan is revisable and a replaced one
/// that happened to keep the step count would otherwise be worked through as
/// if it were the procedure's own.
fn check_final_step_shape(plan: &Plan, action_steps: usize) -> Result<(), ReplayRefusal> {
    let final_step = plan
        .steps()
        .get(action_steps)
        .ok_or(ReplayRefusal::UnexpectedPlan)?;
    let previous = plan
        .steps()
        .get(action_steps.saturating_sub(1))
        .ok_or(ReplayRefusal::UnexpectedPlan)?;
    if final_step.kind() != StepKind::Extract
        || final_step.dependencies() != [previous.plan_step_id().clone()]
    {
        return Err(ReplayRefusal::UnexpectedPlan);
    }
    Ok(())
}

pub(super) fn advance(plan_step: &PlanStep, to: StepState) -> Command {
    Command::AdvanceStep {
        plan_step_id: plan_step.plan_step_id().clone(),
        to,
    }
}

fn next_final_command<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
    final_step: &PlanStep,
    procedure: &Procedure,
    source: &ConsentedSource,
    page: Option<ReplayPage<'_>>,
    digest: &dyn WorkflowDigest,
) -> Result<Option<Command>, ReplayRefusal> {
    match final_step.state() {
        StepState::Ready => Ok(Some(advance(final_step, StepState::Running))),
        StepState::Running => {
            if reducer.is_web_errand() {
                if !reducer.errand_outcome_witnessed() {
                    return Err(ReplayRefusal::MissingObservationEvidence);
                }
                let started = reducer
                    .actions()
                    .filter(|action| {
                        action.state() == ActionState::Verified
                            && matches!(
                                action.proposal().tool_name(),
                                "browser.download.start" | "browser.download.from_link"
                            )
                    })
                    .count();
                if started > 0 {
                    let Some(page) = page else {
                        return action::refresh_final(
                            reducer, procedure, final_step, source, digest,
                        );
                    };
                    if page
                        .pending_downloads
                        .and_then(|pending| page.completed_downloads.checked_add(pending))
                        != Some(started)
                    {
                        return Ok(Some(Command::FailTask {
                            reason: task_engine::FailureReason::UnverifiableAction,
                        }));
                    }
                    if page.completed_downloads != started {
                        return action::poll_downloads(
                            reducer, procedure, final_step, source, page, digest,
                        );
                    }
                }
            }
            if verified_evidence(reducer).is_none() {
                return Err(ReplayRefusal::MissingObservationEvidence);
            }
            Ok(Some(advance(final_step, StepState::Succeeded)))
        }
        StepState::Succeeded => Ok(Some(Command::ResultCandidateReady)),
        StepState::Pending => Ok(None),
        StepState::Waiting
        | StepState::Skipped
        | StepState::Failed
        | StepState::Cancelled
        | StepState::Superseded => Err(ReplayRefusal::UnexpectedPlan),
    }
}

fn result_command<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
) -> Result<Command, ReplayRefusal> {
    if reducer.is_web_errand() {
        if !reducer.errand_outcome_witnessed() {
            return Err(ReplayRefusal::MissingObservationEvidence);
        }
        return Ok(Command::CompleteResultValidated(TaskResult {
            artifact_ids: Vec::new(),
            unmet: Vec::new(),
            fact_count: 0,
            source_count: 1,
        }));
    }
    let evidence = verified_evidence(reducer).ok_or(ReplayRefusal::MissingObservationEvidence)?;
    let unmet = if evidence.supports_complete_result() {
        Vec::new()
    } else {
        vec![task_engine::template::incomplete_observation_gap()]
    };
    let source_count =
        u64::try_from(reducer.task().snapshot().consented_sources.len()).unwrap_or(u64::MAX);
    let result = TaskResult {
        artifact_ids: Vec::new(),
        unmet,
        fact_count: ObservationGraphSummary::FACT_COUNT,
        source_count,
    };
    if result.is_complete() {
        Ok(Command::CompleteResultValidated(result))
    } else {
        Ok(Command::PartialResultValidated(result))
    }
}

fn verified_evidence<C: Clock, I: IdSource>(
    reducer: &Reducer<C, I>,
) -> Option<&task_engine::PageObservationEvidence> {
    reducer.actions().find_map(|action| {
        (action.state() == ActionState::Verified)
            .then(|| action.observation())
            .flatten()
    })
}
