// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a running task says while it is not simply thinking: a retry, a
//! switch of model, a reply asked for again, a refused move.
//!
//! Each key is the core's closed vocabulary for `status_message_key`; the
//! Android bar maps them to sentences by exact string, so a key changed here
//! is a line lost there.

use task_engine::{ExecutionPhase, ModelAttemptKind, TaskState};

use super::super::task_projection::project_task_view;
use super::task_facts;

fn message(facts: crate::ports::TaskViewFacts) -> String {
    project_task_view(facts)
        .unwrap_or_else(|_| unreachable!())
        .status_message_key
        .unwrap_or_default()
}

#[test]
fn a_paid_attempt_in_flight_names_itself_before_any_phase() {
    let mut facts = task_facts(TaskState::Running);
    facts.execution_phase = Some(ExecutionPhase::Inferencing);
    facts.model_attempt = Some(ModelAttemptKind::Retry);
    assert_eq!(message(facts.clone()), "task.retrying_provider");
    facts.model_attempt = Some(ModelAttemptKind::Failover);
    facts.reply_being_reasked = true;
    facts.last_move_refused = true;
    assert_eq!(message(facts), "task.switching_model");
}

#[test]
fn a_reply_asked_for_again_is_said_while_the_model_is_asked() {
    let mut facts = task_facts(TaskState::Running);
    facts.execution_phase = Some(ExecutionPhase::Inferencing);
    facts.reply_being_reasked = true;
    assert_eq!(message(facts.clone()), "task.asking_again");
    // Once the reply is in and the task moves, the phase speaks again.
    facts.execution_phase = Some(ExecutionPhase::Acting);
    assert_eq!(message(facts), "task.acting");
}

#[test]
fn a_refused_move_is_said_only_until_the_task_moves_again() {
    let mut facts = task_facts(TaskState::Running);
    facts.last_move_refused = true;
    for phase in [
        None,
        Some(ExecutionPhase::Planning),
        Some(ExecutionPhase::Inferencing),
    ] {
        facts.execution_phase = phase;
        assert_eq!(message(facts.clone()), "task.blocked_move", "{phase:?}");
    }
    for (phase, key) in [
        (ExecutionPhase::Observing, "task.observing"),
        (ExecutionPhase::Acting, "task.acting"),
        (ExecutionPhase::Verifying, "task.verifying"),
    ] {
        facts.execution_phase = Some(phase);
        assert_eq!(message(facts.clone()), key);
    }
}

#[test]
fn with_nothing_to_notice_the_phase_alone_speaks() {
    let mut facts = task_facts(TaskState::Running);
    assert_eq!(message(facts.clone()), "task.running");
    facts.execution_phase = Some(ExecutionPhase::Inferencing);
    assert_eq!(message(facts), "task.thinking");
}
