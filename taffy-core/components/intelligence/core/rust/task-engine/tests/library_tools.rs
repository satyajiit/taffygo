// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable model-call, policy, idempotency, and replay proofs for Library tools.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::{DispatchId, TabId};
use task_engine::action::{ActionIntent, LibraryIntent, OpaqueOperandKind};
use task_engine::agent::{ModelReply, ModelToolCall, TurnResidency};
use task_engine::{
    ArgumentValue, Command, Effect, ManualClock, Milestone, ModelStopReason, ProposalDecision,
    Reducer, SequentialIds, SuppliedArgument, TaskState,
};

use common::agent::{page, Digest};

fn argument(name: &str, value: ArgumentValue) -> SuppliedArgument {
    SuppliedArgument::new(name, value)
}

fn calls() -> Vec<(ModelToolCall, bool)> {
    vec![
        (
            ModelToolCall::new(
                "library.search",
                vec![
                    argument("query", ArgumentValue::Text("battery life".to_owned())),
                    argument("limit", ArgumentValue::Count(8)),
                ],
            ),
            false,
        ),
        (
            ModelToolCall::new(
                "library.save",
                vec![
                    argument(
                        "workspace",
                        ArgumentValue::Text("11111111111111111111111111111111".to_owned()),
                    ),
                    argument("workspace_revision", ArgumentValue::Count(14)),
                    argument(
                        "fact",
                        ArgumentValue::Text("22222222222222222222222222222222".to_owned()),
                    ),
                    argument("entry_revision", ArgumentValue::Count(0)),
                ],
            ),
            true,
        ),
        (
            ModelToolCall::new(
                "library.remove",
                vec![
                    argument(
                        "entry",
                        ArgumentValue::Text("33333333333333333333333333333333".to_owned()),
                    ),
                    argument("entry_revision", ArgumentValue::Count(4)),
                ],
            ),
            true,
        ),
    ]
}

fn m6_seed() -> task_engine::TaskSeed {
    let mut seed = common::seed();
    seed.snapshot.milestone = Milestone::M6;
    seed
}

fn with_recorded_call(call: ModelToolCall) -> (common::Fixture, TurnResidency) {
    let entry = task_engine::REGISTRY
        .iter()
        .find(|entry| entry.name == call.tool_name)
        .unwrap_or_else(|| unreachable!("fixture names a registered tool"));
    assert_eq!(
        task_engine::validate(entry.definition(), &call.arguments),
        Ok(())
    );
    let mut fixture = common::draft_from(m6_seed());
    fixture.must_apply(Command::StartTask(common::preview()));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture.must_apply(Command::SetPlan(common::plan_draft()));

    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let (page, _) = page();
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: task_engine::TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![call],
    };
    let residency = TurnResidency::read(call_id, page, reply)
        .unwrap_or_else(|| unreachable!("one bounded call is resident"));
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    fixture.must_apply(Command::RecordModelTurn {
        call_id: residency.call_id().clone(),
        digest: Box::new(residency.digest(&dispositions)),
    });
    (fixture, residency)
}

fn proposal_for(
    fixture: &common::Fixture,
    residency: &TurnResidency,
) -> Box<task_engine::ActionProposal> {
    let dispositions = fixture.reducer.turn_dispositions(residency);
    let next = fixture
        .reducer
        .next_agent_command(Some(residency), &Digest)
        .unwrap_or_else(|error| unreachable!("the bounded call is readable: {error:?}"));
    let Some(Command::ProposeAction(proposal)) = next else {
        unreachable!("a served Library call proposes an action, got {next:?} from {dispositions:?}")
    };
    proposal
}

#[test]
fn all_library_model_calls_project_to_exact_typed_intents() {
    for (call, _) in calls() {
        let (fixture, residency) = with_recorded_call(call);
        let proposal = proposal_for(&fixture, &residency);
        assert_eq!(proposal.tab_id(), &TabId::new("tab_1"));
        match proposal.intent() {
            ActionIntent::Library(LibraryIntent::Search { query, limit, .. }) => {
                assert_eq!(*limit, 8);
                assert_eq!(query.kind(), OpaqueOperandKind::LibraryQuery);
                assert!(!query.handle().contains("battery life"));
            }
            ActionIntent::Library(LibraryIntent::Save {
                workspace_id,
                workspace_revision,
                fact_id,
                entry_revision,
                ..
            }) => {
                assert_eq!(workspace_id, "11111111111111111111111111111111");
                assert_eq!(*workspace_revision, 14);
                assert_eq!(fact_id, "22222222222222222222222222222222");
                assert_eq!(*entry_revision, 0);
            }
            ActionIntent::Library(LibraryIntent::Remove {
                entry_id,
                entry_revision,
                ..
            }) => {
                assert_eq!(entry_id, "33333333333333333333333333333333");
                assert_eq!(*entry_revision, 4);
            }
            other => unreachable!("Library registry row produced {other:?}"),
        }
    }
}

#[test]
fn library_dispatch_is_single_shot_and_replay_never_reexecutes_it() {
    for (call, requires_approval) in calls() {
        let (mut fixture, residency) = with_recorded_call(call);
        let proposal = proposal_for(&fixture, &residency);
        fixture.must_apply(Command::ProposeAction(proposal));
        let action_id = fixture.reducer.actions().last().map_or_else(
            || unreachable!("proposal minted an action"),
            |action| action.action_id().clone(),
        );

        if requires_approval {
            fixture.must_apply(Command::RecordPolicyDecision {
                action_id: action_id.clone(),
                decision: Box::new(ProposalDecision::RequireApproval),
                dispatch_id: None,
            });
            fixture.must_apply(Command::ApproveAction {
                approval: common::receipt(),
                still_current: true,
                expires_at_monotonic_ms: 50_000,
                expires_at_utc_ms: 50_000,
                browser_session_id: task_engine::BrowserSessionId::new("browser_session_1")
                    .unwrap_or_else(|_| unreachable!("valid session identity")),
            });
        }

        let envelope = fixture.envelope(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(common::authorize()),
            dispatch_id: Some(DispatchId::new("dispatch-library")),
        });
        let first = fixture
            .reducer
            .apply(envelope.clone())
            .unwrap_or_else(|error| unreachable!("authorized Library dispatch: {error:?}"));
        assert_eq!(
            first.effects,
            vec![Effect::RunLibraryTool {
                action_id: action_id.clone(),
            }]
        );
        let journal_len = fixture.reducer.journal().len();

        let duplicate = fixture
            .reducer
            .apply(envelope)
            .unwrap_or_else(|error| unreachable!("exact duplicate is recognized: {error:?}"));
        assert!(duplicate.duplicate);
        assert!(duplicate.effects.is_empty());
        assert_eq!(fixture.reducer.journal().len(), journal_len);

        let (replayed, recovery) = Reducer::replay(
            m6_seed(),
            common::defaults(),
            ManualClock::at(9_000),
            SequentialIds::new(),
            fixture.reducer.journal(),
        )
        .unwrap_or_else(|error| unreachable!("Library journal replays: {error:?}"));
        assert_eq!(recovery.unknown_outcome_actions, vec![action_id.clone()]);
        assert_eq!(replayed.task().state(), TaskState::Running);
        assert_eq!(
            replayed
                .action(&action_id)
                .map(task_engine::ActionRecord::state),
            Some(task_engine::ActionState::OutcomeUnknown)
        );
    }
}
