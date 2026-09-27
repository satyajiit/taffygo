// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one scheduler walk, driven through the public facade.
//!
//! An integration test on purpose: the doubles live in `kernel-test-support`,
//! which links this crate once, so the trait identities unify — a unit test
//! would compile a second copy of the crate and they would not.

// A test asserting an envelope exists unwraps it; a panic is the failure.
#![allow(clippy::unwrap_used, clippy::expect_used)]

use bip_types::identity::TaskId;
use core_runtime::account::{DigestError, Sha256Port};
use core_runtime::{
    next_agent_command_envelope, next_durable_command_envelope,
    next_durable_command_envelope_with_procedure, next_reviewed_command_envelope,
    workflow_for_provider_route, ReviewedWorkflowError, TaskWorkflow,
};
use kernel_test_support::{ReferenceDigest, ScriptedTask};
use task_engine::{Command, ModelCallId, TaskJournal, REVIEWED_NO_MODEL_ROUTE_ID};

mod route_choice {
    use super::*;

    fn procedure() -> procedure_engine::Procedure {
        let origin =
            policy_engine::origin::normalize_serialization("https://example.test").unwrap();
        procedure_engine::build_source_table(&origin).unwrap()
    }

    #[test]
    fn the_consented_route_picks_the_scheduler() {
        assert_eq!(
            workflow_for_provider_route(Some(REVIEWED_NO_MODEL_ROUTE_ID)),
            TaskWorkflow::Reviewed
        );
        assert_eq!(
            workflow_for_provider_route(Some("direct_user_key")),
            TaskWorkflow::Agent
        );
        assert_eq!(
            workflow_for_provider_route(Some("managed_service")),
            TaskWorkflow::Agent
        );
        assert_eq!(workflow_for_provider_route(None), TaskWorkflow::Unspecified);
    }

    #[test]
    fn a_reviewed_route_does_not_fall_through_to_the_assistant_table() {
        let task = task(
            Some(REVIEWED_NO_MODEL_ROUTE_ID),
            None,
            Some(Command::RequestModelTurn {
                call_id: ModelCallId::new("model-call-1"),
            }),
        );

        assert_eq!(
            next_durable_command_envelope(&task, None, &ReferenceDigest),
            Ok(None)
        );
    }

    #[test]
    fn a_reviewed_route_uses_the_reviewed_command() {
        let task = task(
            Some(REVIEWED_NO_MODEL_ROUTE_ID),
            Some(Command::ExecutorStarted),
            Some(Command::RequestModelTurn {
                call_id: ModelCallId::new("model-call-1"),
            }),
        );

        let envelope = next_durable_command_envelope(&task, None, &ReferenceDigest)
            .unwrap()
            .unwrap();
        assert_eq!(envelope.command, Command::ExecutorStarted);
        assert!(envelope.idempotency_key.as_str().starts_with("reviewed-"));
    }

    #[test]
    fn an_agent_route_uses_the_assistant_table_even_when_reviewed_would_answer() {
        let task = task(
            Some("direct_user_key"),
            Some(Command::ExecutorStarted),
            Some(Command::RequestModelTurn {
                call_id: ModelCallId::new("model-call-1"),
            }),
        );

        let envelope = next_durable_command_envelope(&task, None, &ReferenceDigest)
            .unwrap()
            .unwrap();
        assert_eq!(
            envelope.command,
            Command::RequestModelTurn {
                call_id: ModelCallId::new("model-call-1"),
            }
        );
        assert!(envelope.idempotency_key.as_str().starts_with("agent-"));
    }

    #[test]
    fn an_undisclosed_route_asks_neither_table() {
        let task = task(
            None,
            Some(Command::ExecutorStarted),
            Some(Command::RequestModelTurn {
                call_id: ModelCallId::new("model-call-1"),
            }),
        );

        assert_eq!(
            next_durable_command_envelope(&task, None, &ReferenceDigest),
            Ok(None)
        );
    }

    #[test]
    fn a_selected_procedure_owns_the_scheduler_and_replay_identity() {
        let mut task = task(
            Some("direct_user_key"),
            None,
            Some(Command::RequestModelTurn {
                call_id: ModelCallId::new("agent-call-1"),
            }),
        );
        task.procedure = Some(Command::ExecutorStarted);
        let procedure = procedure();

        let first = next_durable_command_envelope_with_procedure(
            &task,
            Some(&procedure),
            None,
            &ReferenceDigest,
        )
        .unwrap()
        .unwrap();
        let replayed = next_durable_command_envelope_with_procedure(
            &task,
            Some(&procedure),
            None,
            &ReferenceDigest,
        )
        .unwrap()
        .unwrap();

        assert_eq!(first.command, Command::ExecutorStarted);
        assert!(first.idempotency_key.as_str().starts_with("procedure-"));
        assert_eq!(first.idempotency_key, replayed.idempotency_key);
        assert_eq!(first.trace_id, replayed.trace_id);
    }

    #[test]
    fn a_waiting_selected_procedure_never_falls_through_to_the_model() {
        let task = task(
            Some("direct_user_key"),
            None,
            Some(Command::RequestModelTurn {
                call_id: ModelCallId::new("agent-call-1"),
            }),
        );
        assert_eq!(
            next_durable_command_envelope_with_procedure(
                &task,
                Some(&procedure()),
                None,
                &ReferenceDigest,
            ),
            Ok(None)
        );
    }

    #[test]
    fn the_same_durable_revision_returns_the_same_agent_envelope_identity() {
        let task = task(
            Some("direct_user_key"),
            None,
            Some(Command::RequestModelTurn {
                call_id: ModelCallId::new("model-call-1"),
            }),
        );

        let first = next_agent_command_envelope(&task, None, &ReferenceDigest)
            .unwrap()
            .unwrap();
        let second = next_agent_command_envelope(&task, None, &ReferenceDigest)
            .unwrap()
            .unwrap();

        assert_eq!(first.idempotency_key, second.idempotency_key);
        assert_eq!(first.trace_id, second.trace_id);
        assert_eq!(first.expected_revision, 7);
        assert!(first.idempotency_key.as_str().starts_with("agent-"));
    }

    fn task(
        route: Option<&'static str>,
        reviewed: Option<Command>,
        agent: Option<Command>,
    ) -> ScriptedTask {
        ScriptedTask {
            id: TaskId::new("task-durable"),
            revision: 7,
            route,
            reviewed,
            agent,
            procedure: None,
            journal: TaskJournal::new(),
            transcript: None,
        }
    }
}

mod walk_eviction {
    use loop_kernel::context::{RecordedCall, TaskTranscript, TranscriptBudget, TurnExchange};
    use loop_kernel::state::LoopState;
    use loop_kernel::walk;
    use model_router::json::JsonValue;
    use task_engine::ActionState;

    use super::*;

    const BUDGET: usize = 1400;

    fn exchange(ordinal: u64) -> TurnExchange {
        let call = RecordedCall::new(
            format!("call-{ordinal}"),
            "a".repeat(600),
            JsonValue::Object(std::collections::BTreeMap::new()),
            ActionState::Proposed,
        );
        TurnExchange::new(ordinal, vec![call]).unwrap()
    }

    fn task(transcript: TaskTranscript) -> ScriptedTask {
        ScriptedTask {
            id: TaskId::new("task-evicting"),
            revision: 7,
            route: Some("direct_user_key"),
            reviewed: None,
            agent: Some(Command::RequestModelTurn {
                call_id: ModelCallId::new("model-call-1"),
            }),
            procedure: None,
            journal: TaskJournal::new(),
            transcript: Some(transcript),
        }
    }

    #[test]
    fn a_ladder_drop_becomes_a_durable_boundary_before_the_turn() {
        let transcript = TaskTranscript::new(
            "goal".to_owned(),
            vec![exchange(0), exchange(1), exchange(2)],
            TranscriptBudget::new(BUDGET),
        );
        let task = task(transcript);
        let planned = walk::advance(&task, &mut LoopState::default(), &ReferenceDigest, 1, 1_000)
            .unwrap()
            .unwrap();
        assert_eq!(
            planned.envelope.command,
            Command::RecordContextEviction { through_turn: 1 }
        );
        assert_eq!(
            planned.envelope.idempotency_key.as_str(),
            "context-eviction-1"
        );
        assert_eq!(
            planned.operation.operation_id,
            "reviewed-operation-context-eviction-1"
        );
        assert!(!planned.staged_ask);
    }

    #[test]
    fn a_settled_boundary_lets_the_turn_through() {
        let transcript = TaskTranscript::with_floor(
            "goal".to_owned(),
            vec![exchange(0), exchange(1), exchange(2)],
            TranscriptBudget::new(BUDGET),
            Some(1),
        );
        let task = task(transcript);
        let planned = walk::advance(&task, &mut LoopState::default(), &ReferenceDigest, 1, 1_000)
            .unwrap()
            .unwrap();
        assert_eq!(
            planned.envelope.command,
            Command::RequestModelTurn {
                call_id: ModelCallId::new("model-call-1"),
            }
        );
    }

    #[test]
    fn the_conversation_material_is_identical_before_and_after_the_boundary_commits() {
        // The prompt-prefix stability invariant of decision 0074, at the
        // material level: the exchanges kept, the elision line and the byte
        // count are the same whether the ladder dropped the turns this
        // compose or the journaled floor drops them on every compose after.
        let exchanges = vec![exchange(0), exchange(1), exchange(2)];
        let before = TaskTranscript::new(
            "goal".to_owned(),
            exchanges.clone(),
            TranscriptBudget::new(BUDGET),
        );
        let after = TaskTranscript::with_floor(
            "goal".to_owned(),
            exchanges,
            TranscriptBudget::new(BUDGET),
            Some(1),
        );
        assert_eq!(before.goal(), after.goal());
        assert_eq!(before.exchanges(), after.exchanges());
        assert_eq!(before.elided(), after.elided());
        assert_eq!(before.elision_notice(), after.elision_notice());
        assert_eq!(before.text_bytes(), after.text_bytes());
    }
}

mod reviewed_envelopes {
    use super::*;

    #[test]
    fn the_same_durable_revision_returns_the_same_envelope_identity() {
        let task = task(7, Some(Command::ExecutorStarted));

        let first = next_reviewed_command_envelope(&task, &ReferenceDigest)
            .unwrap()
            .unwrap();
        let second = next_reviewed_command_envelope(&task, &ReferenceDigest)
            .unwrap()
            .unwrap();

        assert_eq!(first.idempotency_key, second.idempotency_key);
        assert_eq!(first.trace_id, second.trace_id);
        assert_eq!(first.expected_revision, 7);
        assert_eq!(first.command, Command::ExecutorStarted);
    }

    #[test]
    fn identity_changes_with_revision_and_none_stays_none() {
        let first = next_reviewed_command_envelope(
            &task(7, Some(Command::ExecutorStarted)),
            &ReferenceDigest,
        )
        .unwrap()
        .unwrap();
        let second = next_reviewed_command_envelope(
            &task(8, Some(Command::ExecutorStarted)),
            &ReferenceDigest,
        )
        .unwrap()
        .unwrap();

        assert_ne!(first.idempotency_key, second.idempotency_key);
        assert_eq!(
            next_reviewed_command_envelope(&task(8, None), &ReferenceDigest),
            Ok(None)
        );
    }

    #[test]
    fn digest_failure_is_not_a_fallback_command() {
        struct Unavailable;
        impl Sha256Port for Unavailable {
            fn sha256(&self, _input: &[u8]) -> Result<[u8; 32], DigestError> {
                Err(DigestError::Unavailable)
            }
        }

        assert_eq!(
            next_reviewed_command_envelope(&task(7, Some(Command::ExecutorStarted)), &Unavailable),
            Err(ReviewedWorkflowError::DigestUnavailable)
        );
    }

    fn task(revision: u64, command: Option<Command>) -> ScriptedTask {
        ScriptedTask {
            id: TaskId::new("task-reviewed"),
            revision,
            route: None,
            reviewed: command,
            agent: None,
            procedure: None,
            journal: TaskJournal::new(),
            transcript: None,
        }
    }
}
