// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed Python calls from model reply to the content-free action intent.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use task_engine::action::{ActionIntent, OpaqueOperandKind, PythonEntrypoint};
use task_engine::agent::{ModelToolCall, TurnResidency};
use task_engine::{
    loop_tool_result, ArgumentValue, CallVerdict, Command, LoopOutcome, Milestone, ModelStopReason,
    NotAttempted, SuppliedArgument, WorkflowDigest, WorkflowError,
};

use common::agent::{activate_tool_call, page, reply};

const TITLE: &str = "MODEL_ONLY_TITLE_0123456789";
const CONTENT: &str = "MODEL_ONLY_CONTENT_0123456789_abcdefghijklmnopqrstuvwxyz";

struct OpaqueDigest;

impl WorkflowDigest for OpaqueDigest {
    fn sha256(&self, _input: &[u8]) -> Result<[u8; 32], WorkflowError> {
        Ok([0xa5; 32])
    }
}

fn argument(name: &str, value: ArgumentValue) -> SuppliedArgument {
    SuppliedArgument::new(name, value)
}

fn call(entrypoint: &str) -> ModelToolCall {
    ModelToolCall::new(
        "python.execute",
        vec![
            argument("entrypoint", ArgumentValue::Choice(entrypoint.to_owned())),
            argument("title", ArgumentValue::Text(TITLE.to_owned())),
            argument("content", ArgumentValue::Text(CONTENT.to_owned())),
        ],
    )
}

fn recorded(calls: Vec<ModelToolCall>) -> (common::Fixture, TurnResidency) {
    let mut seed = common::seed();
    seed.snapshot.milestone = Milestone::M7;
    let mut fixture = common::draft_from(seed);
    fixture.must_apply(Command::StartTask(common::preview()));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);

    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let (turn_page, _) = page();
    let residency =
        TurnResidency::read(call_id, turn_page, reply(ModelStopReason::ToolCall, calls))
            .expect("the fixture page is readable");
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    fixture.must_apply(Command::RecordModelTurn {
        call_id: residency.call_id().clone(),
        digest: Box::new(residency.digest(&dispositions)),
    });
    (fixture, residency)
}

fn activate(fixture: &common::Fixture, residency: &mut TurnResidency) {
    let (sequence, pending, entry) = fixture
        .reducer
        .pending_loop_call(residency)
        .expect("tool.activate waits for loop settlement");
    let tools = task_engine::EffectiveToolSet::for_task(Milestone::M7, &[]);
    let (outcome, result) = loop_tool_result(entry, pending, &tools);
    assert_eq!(outcome, LoopOutcome::Activated { name_known: true });
    assert!(residency.settle_loop_with_result(sequence, outcome, result));
}

#[test]
fn a_registered_choice_becomes_one_content_free_typed_job() {
    for (choice, expected) in [
        ("document", PythonEntrypoint::DocumentBuild),
        ("spreadsheet", PythonEntrypoint::SpreadsheetBuild),
    ] {
        let (fixture, mut residency) =
            recorded(vec![activate_tool_call("python.execute"), call(choice)]);
        activate(&fixture, &mut residency);
        let command = fixture
            .reducer
            .next_agent_command(Some(&residency), &OpaqueDigest)
            .expect("decision");
        let Some(Command::ProposeAction(proposal)) = command else {
            panic!("expected Python proposal, got {command:?}");
        };
        let ActionIntent::ToolJob(intent) = proposal.intent() else {
            panic!("expected typed Python job");
        };
        assert_eq!(intent.tool_name, "python.execute");
        assert_eq!(intent.runtime, task_engine::ToolRuntime::Python);
        assert_eq!(intent.entrypoint, expected);
        assert_eq!(intent.title.kind(), OpaqueOperandKind::PythonTitle);
        assert_eq!(intent.content.kind(), OpaqueOperandKind::PythonContent);
        assert_eq!(intent.tab.as_str(), "tab_1");

        let encoded = proposal
            .intent()
            .encode_canonical()
            .expect("bounded intent");
        assert_eq!(
            ActionIntent::decode_canonical(&encoded),
            Ok(proposal.intent().clone())
        );
        for transient in [TITLE.as_bytes(), CONTENT.as_bytes()] {
            assert!(
                !encoded
                    .windows(transient.len())
                    .any(|window| window == transient),
                "{choice}"
            );
        }
    }
}

#[test]
fn model_authored_code_fields_never_become_an_action() {
    for forbidden in ["source", "code", "module", "path", "argv"] {
        let mut injected = call("document");
        injected.arguments.push(argument(
            forbidden,
            ArgumentValue::Text("print('not data')".to_owned()),
        ));
        let (fixture, mut residency) =
            recorded(vec![activate_tool_call("python.execute"), injected]);
        activate(&fixture, &mut residency);
        let dispositions = fixture.reducer.turn_dispositions(&residency);
        let Some(rejected) = dispositions.get(1) else {
            panic!("expected activation and injected Python calls");
        };
        assert_eq!(
            rejected.verdict,
            CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected),
            "{forbidden}"
        );
        assert!(fixture
            .reducer
            .next_agent_command(Some(&residency), &OpaqueDigest)
            .expect("decision")
            .is_some_and(|command| matches!(command, Command::RequestModelTurn { .. })));
    }
}
