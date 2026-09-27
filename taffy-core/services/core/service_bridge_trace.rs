// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Content-free diagnostic lines for a task's walk, tagged `[taffy_trace]`.
//!
//! The journal records what a task did, and the log said almost nothing about
//! it: an errand could spend its whole model budget with one
//! `[taffy_task_effect_completed]` line per reply and no word of what each
//! reply asked for, what policy said, or what the page did. These lines are
//! that account, one per command a task submits and one per reply read.
//!
//! Every line is composed from compiled-in names and counts only — a
//! command's kind, a registered tool name, a result code, a stop reason, a
//! token count. None carries page, prompt, reply, query or address text, the
//! rule every other line in this service keeps; a proposal says whether it
//! names a destination, never which.

use core_runtime::{Command, CommandEnvelope, HistoryAdmission, ProposalDecision, TaskId};

use crate::service_bridge_trace_ffi::ffi;

/// A command about to be submitted, described before the runtime takes it.
pub(crate) struct Step {
    line: String,
}

/// Describes `envelope` for the line [`submitted`] writes once admission has
/// answered.
pub(crate) fn step(task_id: &TaskId, envelope: &CommandEnvelope) -> Step {
    Step {
        line: format!(
            "step task={} rev={} cmd={}{}",
            short(task_id),
            envelope.expected_revision,
            envelope.command.kind().label(),
            detail(&envelope.command),
        ),
    }
}

/// Writes one submitted command beside the admission it met.
pub(crate) fn submitted(step: Step, admission: &str) {
    ffi::TaskTrace(&format!("{} admission={admission}", step.line));
}

/// Writes how one model reply was read: `read` beside the registered name of
/// every call it made, or the reason it was a gap, and the clause that refused
/// each call on sight.
pub(crate) fn reply_read(
    task_id: &TaskId,
    via: &str,
    outcome: &str,
    tools: &[&'static str],
    refused: &[&'static str],
) {
    ffi::TaskTrace(&format!(
        "reply task={} via={via} outcome={outcome} tools=[{}] refused=[{}]",
        short(task_id),
        tools.join(","),
        refused.join(","),
    ));
}

/// Writes that the walk found nothing to start: the task is waiting on an
/// effect, a person, or is over.
pub(crate) fn idle(task_id: &TaskId, why: &str) {
    ffi::TaskTrace(&format!("walk task={} idle={why}", short(task_id)));
}

/// Writes that one committed task was refused at bootstrap, and the
/// compiled-in name of why (decision 0235).
pub(crate) fn restore_refused(task_id: &str, label: &str) {
    ffi::TaskTrace(&format!(
        "restore task={} refused={label}",
        short_id(task_id)
    ));
}

/// Writes that one committed task came back with journalled commands applied
/// over a bound on its conduct this build keeps and the build that journalled
/// them did not — how many, and the first (decision 0235). Silent when there
/// were none, which is every journal this build wrote itself.
pub(crate) fn restored_over_history(task_id: &TaskId, admitted: &[HistoryAdmission]) {
    let Some(first) = admitted.first() else {
        return;
    };
    ffi::TaskTrace(&format!(
        "restore task={} admitted_as_history={} first={} bound={}",
        short(task_id),
        admitted.len(),
        first.command.label(),
        first.bound.label(),
    ));
}

/// The last eight characters of a task identity: enough to tell two tasks
/// apart in one log, and no more of an identifier than a line needs.
fn short(task_id: &TaskId) -> &str {
    short_id(task_id.as_str())
}

fn short_id(id: &str) -> &str {
    id.get(id.len().saturating_sub(8)..).unwrap_or(id)
}

/// What a command carries that a reader of the log needs, in names and counts.
fn detail(command: &Command) -> String {
    match command {
        Command::RequestModelAttempt {
            attempt_ordinal,
            candidate_ordinal,
            kind,
            ..
        } => format!(
            " attempt={attempt_ordinal} candidate={candidate_ordinal} kind={}",
            kind.label()
        ),
        Command::RecordModelTurn { digest, .. } => format!(
            " stop={} calls={} refused_on_sight={} answer_segments={} in={} out={} \
             cache_read={} page={} offered={} omitted={} unreadable={}",
            digest.stop.label(),
            digest.tool_calls,
            digest.refused_tool_calls,
            digest.answer_segments,
            digest.usage.input_units,
            digest.usage.output_units,
            digest.usage.cache_read_units,
            digest.render.readability.label(),
            digest.render.offered_nodes,
            digest.render.omitted_nodes,
            digest.render.unreadable_nodes,
        ),
        Command::RecordModelTurnGap { gap, .. } => format!(" gap={}", gap.label()),
        Command::ProposeAction(proposal) => format!(
            " tool={} class={} node={} destination={}",
            proposal.tool_name(),
            proposal.action_class().label(),
            proposal.node_id().is_some(),
            proposal.destination_address().is_some(),
        ),
        Command::RecordPolicyDecision { decision, .. } => match decision.as_ref() {
            ProposalDecision::Authorize(_) => " decision=authorize".to_owned(),
            ProposalDecision::RequireApproval => " decision=require_approval".to_owned(),
            ProposalDecision::Deny(denial) => format!(" decision=deny:{}", denial.code.wire()),
        },
        Command::RecordActionOutcome { outcome, .. } => format!(
            " code={} observed={} new_source={}",
            outcome.code.wire(),
            outcome.observation.is_some(),
            outcome.discovered_source.is_some(),
        ),
        Command::FailTask { reason } => format!(" reason={}", reason.label()),
        Command::PauseTask { cause } => format!(" cause={}", cause.label()),
        Command::RecordContextEviction { through_turn } => {
            format!(" through_turn={through_turn}")
        }
        _ => String::new(),
    }
}
