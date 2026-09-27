// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A refused discovery bootstrap ends the task from the walk.

use task_engine::{Command, FailureReason, TaskState};

use super::super::{advance, discovery_failure_command};
use super::LoopTask;
use crate::digest::{DigestError, Sha256Port};
use crate::state::LoopState;

struct NoDigest;

impl Sha256Port for NoDigest {
    fn sha256(&self, _input: &[u8]) -> Result<[u8; 32], DigestError> {
        Ok([0; 32])
    }
}

#[test]
fn a_refused_discovery_bootstrap_fails_the_task_once_it_is_running() {
    let mut state = LoopState {
        discovery_unavailable: true,
        ..LoopState::default()
    };
    // Queued: the reducer would refuse the failure, so the flag waits for the
    // hop that gets the task running rather than being spent on a refusal.
    let queued = LoopTask::new();
    let planned = advance(&queued, &mut state, &NoDigest, 3, 1_000)
        .unwrap_or_else(|error| unreachable!("a waiting task plans nothing: {error:?}"));
    assert!(planned.is_none());
    assert!(state.discovery_unavailable);

    let running = LoopTask {
        state: Some(TaskState::Running),
        ..LoopTask::new()
    };
    let planned = advance(&running, &mut state, &NoDigest, 3, 1_000)
        .unwrap_or_else(|error| unreachable!("the failure identity is bounded: {error:?}"))
        .unwrap_or_else(|| unreachable!("a running task with no discovery tab ends"));
    assert_eq!(
        planned.envelope.command,
        Command::FailTask {
            reason: FailureReason::SourcesUnavailable,
        }
    );
    assert!(!planned.staged_ask);
    assert!(!state.discovery_unavailable);
    let again = discovery_failure_command(1, 3, 1_000)
        .unwrap_or_else(|error| unreachable!("the same input is valid: {error:?}"));
    assert_eq!(planned.envelope, again.envelope);
    assert_eq!(planned.operation, again.operation);
}
