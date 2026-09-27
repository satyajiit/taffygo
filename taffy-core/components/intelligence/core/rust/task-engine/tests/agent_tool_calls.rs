// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The tool-call half of the agent decision table: row 21 and the readings a
//! call is refused by.
//!
//! Split from `agent_decision_table` along the seam the table itself draws.
//! That file drives the rows about a *turn* — asked, answered, gapped,
//! contradictory. This one drives the rows about the *calls inside one
//! answer*: the order they are attempted in, the refusal that ends the walk,
//! and the four ways a call is refused on sight.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{DispatchId, MonotonicMillis, TabId};
use bip_types::ActionResultCode;
use task_engine::action::ActionOutcome;
use task_engine::agent::ModelToolCall;
use task_engine::{
    ArgumentValue, CallVerdict, Command, ModelStopReason, NotAttempted, SuppliedArgument,
};

use common::agent::{page, read_call, with_recorded_turn, Digest};
use common::Fixture;

/// Proposes, authorizes, dispatches and refuses one whole-document read, so
/// that the call the model keeps asking for accumulates refusals in the
/// reducer's own ledger.
fn refuse_a_read(fixture: &mut Fixture, round: usize) {
    let proposed = common::proposal_for(
        task_engine::action::BrowserIntent::DomRead {
            tab: TabId::new("tab_1"),
            target: None,
        },
        &format!("loop_key_{round}"),
    );
    fixture.must_apply(Command::ProposeAction(Box::new(proposed)));
    let action_id = fixture
        .reducer
        .actions()
        .last()
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    let dispatch = format!("dispatch_{round}");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(common::authorize()),
        dispatch_id: Some(DispatchId::new(&dispatch)),
    });
    fixture.must_apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::NodeGone,
            dispatch_id: Some(DispatchId::new(&dispatch)),
            observed_at: MonotonicMillis(10),
            observation: None,
            discovered_source: None,
        }),
    });
}

#[test]
fn row_21a_a_refused_call_refuses_every_call_the_model_ordered_after_it() {
    // The second mandatory row. The first call names a tool this milestone has
    // not reached; the two well-formed calls behind it are refused with
    // `PriorCallRefused` rather than run, because the model ordered them and a
    // later call may depend on an earlier one having happened.
    let (_, handle) = page();
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![
            ModelToolCall::new("browser.form.fill", Vec::new()),
            read_call(handle),
            read_call(handle),
        ],
    );

    let dispositions = fixture.reducer.turn_dispositions(&residency);
    assert_eq!(dispositions.len(), 3);
    assert_eq!(
        dispositions[0].verdict,
        CallVerdict::NotAttempted(NotAttempted::ToolNotAvailable)
    );
    assert_eq!(
        dispositions[1].verdict,
        CallVerdict::NotAttempted(NotAttempted::PriorCallRefused)
    );
    assert_eq!(
        dispositions[2].verdict,
        CallVerdict::NotAttempted(NotAttempted::PriorCallRefused)
    );

    // Only the refusal visible on the face of the reply is counted in the
    // durable record; the two behind it are calls the turn named and never
    // proposed, which the journal shows as proposals that do not exist.
    assert_eq!(residency.digest(&dispositions).refused_tool_calls, 1);

    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "nothing behind a refusal is attempted, got {next:?}"
    );
}

#[test]
fn row_21b_the_first_attemptable_call_is_proposed_and_only_that_one() {
    let (_, handle) = page();
    let (mut fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![read_call(handle), read_call(handle)],
    );

    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected the first call to be proposed, got {next:?}");
    };
    assert_eq!(proposal.tool_name(), "browser.dom.read");
    assert_eq!(proposal.idempotency_key.as_str(), "turn-0-call-0");
    // The shipping read executor is document-scoped, so no model-issued node
    // handle is silently widened to the page.
    assert_eq!(proposal.node_id(), None);
    assert_eq!(proposal.tab_id(), &TabId::new("tab_1"));
    // The class comes from the compiled-in row, never from the reply.
    assert_eq!(
        proposal.action_class(),
        task_engine::ActionClass::ObservePage
    );
    assert_eq!(
        proposal.idempotency(),
        task_engine::IdempotencyClass::PureRead
    );

    // One at a time: with the first proposal committed and still in flight,
    // the loop proposes nothing further.
    fixture.must_apply(Command::ProposeAction(proposal));
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap(),
        None
    );
}

#[test]
fn row_21e_a_call_that_did_not_succeed_ends_the_turn() {
    let (_, handle) = page();
    let (mut fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![read_call(handle), read_call(handle)],
    );
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected a proposal, got {next:?}");
    };
    fixture.must_apply(Command::ProposeAction(proposal));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == "turn-0-call-0")
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id,
        decision: Box::new(task_engine::ProposalDecision::Deny(
            task_engine::Denial::new(bip_types::ActionResultCode::DeniedByPolicy),
        )),
        dispatch_id: None,
    });

    // The second call is not run on the strength of the first being refused.
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "a refused call must end the turn, got {next:?}"
    );
}

#[test]
fn row_20_a_recorded_turn_whose_reply_is_gone_asks_again_rather_than_inventing_one() {
    let (_, handle) = page();
    let (fixture, _residency) =
        with_recorded_turn(ModelStopReason::ToolCall, vec![read_call(handle)]);
    let next = fixture.reducer.next_agent_command(None, &Digest).unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "expected a fresh turn, got {next:?}"
    );
}

#[test]
fn a_targeted_read_is_refused_before_a_handle_can_be_resolved() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "browser.dom.read",
            vec![SuppliedArgument::new("node", ArgumentValue::Handle(4_242))],
        )],
    );
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    assert_eq!(
        dispositions[0].verdict,
        CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected)
    );
}

#[test]
fn arguments_that_do_not_match_the_compiled_in_schema_are_refused_and_never_coerced() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "browser.dom.read",
            vec![SuppliedArgument::new(
                "node",
                ArgumentValue::Text("the login button".to_owned()),
            )],
        )],
    );
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    assert_eq!(
        dispositions[0].verdict,
        CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected)
    );
}

#[test]
fn a_deterministic_artifact_runtime_is_attemptable() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "artifact.markdown.create",
            vec![SuppliedArgument::new(
                "title",
                ArgumentValue::Text("Comparison".to_owned()),
            )],
        )],
    );
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    assert_eq!(dispositions[0].verdict, CallVerdict::Attemptable);
}

#[test]
fn a_call_that_reaches_the_person_stops_the_turn_instead_of_asking_forever() {
    let (mut fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "user.ask",
            vec![SuppliedArgument::new(
                "subject",
                ArgumentValue::Text("which of the two".to_owned()),
            )],
        )],
    );
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap(),
        Some(Command::RequestUserInput)
    );
    fixture.must_apply(Command::RequestUserInput);
    assert_eq!(fixture.state(), task_engine::TaskState::WaitingUser);
    fixture.must_apply(Command::SupplyUserInput);
    assert_eq!(fixture.state(), task_engine::TaskState::Running);

    // Back in RUNNING with the same residency still in hand, the loop must not
    // walk the same reply and ask the same question again.
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "expected a fresh turn, got {next:?}"
    );
}

#[test]
fn a_call_refused_its_way_to_the_ceiling_is_not_attempted_again() {
    // The loop guard is wired into the *reading* of a reply, not only into the
    // guard that would have refused the proposal afterwards. That matters
    // because the two answers are different records: refused at the guard, the
    // turn has proposed an action and had it thrown out; refused here, the call
    // is `NotAttempted(RepeatedRefusalsAbandoned)` and the model reads that
    // back in its own transcript and re-plans against it (decision 0052
    // section 3).
    //
    // Driven through the real ledger — propose, authorize, dispatch, refuse,
    // three times — rather than by reaching into it, so what is asserted is
    // that `read_call` computes the same fingerprint
    // `Guard::NotLoopingOnRefusals` computes. A second copy of the fingerprint
    // rule would agree with itself forever.
    let (_, handle) = page();
    let (mut fixture, residency) =
        with_recorded_turn(ModelStopReason::ToolCall, vec![read_call(handle)]);
    for round in 0..usize::try_from(task_engine::MAX_IDENTICAL_REFUSALS).unwrap() {
        refuse_a_read(&mut fixture, round);
    }
    assert!(fixture
        .reducer
        .refusals()
        .is_abandoned(task_engine::CallFingerprint::of_target(
            "browser.dom.read",
            Some("tab_1"),
            None,
        )));

    let dispositions = fixture.reducer.turn_dispositions(&residency);
    assert_eq!(
        dispositions[0].verdict,
        CallVerdict::NotAttempted(NotAttempted::RepeatedRefusalsAbandoned)
    );

    // And the loop does not propose it: the answer is another turn, with the
    // refusal in the record the model is shown.
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "an abandoned call must not be proposed again, got {next:?}"
    );
}

#[test]
fn the_minted_call_identity_satisfies_the_strictest_protocol_family() {
    // The idempotency key of a proposal is also the identity a replayed tool
    // call and its result are paired by on the wire, and the strictest of the
    // four protocol families accepts `[a-zA-Z0-9_-]` up to sixty-four
    // characters and silently rewrites anything else. A rewritten identity
    // pairs a result with a call it did not answer — or with none — and
    // nothing sanitizes it anywhere, deliberately.
    //
    // It is asserted here because this is where the format is minted.
    // `model-router` cannot see this function and its own version of this
    // check builds the string from a literal of its own, so a change made
    // here would leave that one passing.
    let (_, handle) = page();
    let (fixture, residency) =
        with_recorded_turn(ModelStopReason::ToolCall, vec![read_call(handle)]);
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected a call to be proposed, got {next:?}");
    };
    let minted = proposal.idempotency_key.as_str();
    assert!(
        !minted.is_empty() && minted.len() <= 64,
        "{minted} is not a length every family accepts"
    );
    assert!(
        minted
            .chars()
            .all(|letter| letter.is_ascii_alphanumeric() || letter == '_' || letter == '-'),
        "{minted} carries a character a family would silently rewrite"
    );
}
