// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Row 21k: a run of turns that changed nothing ends an errand.
//!
//! Every earlier bound counts one shape of turn, and a loop made of two shapes
//! clears each of them on every second turn. Errand `7b367bd8` spent six of
//! thirty-eight turns on a phone alternating a reading of one unchanged page
//! with a click refused `handle_unknown`; row 21j reset on every reading,
//! because a reading is attempted, and the errand ran until its budget did
//! (decisions 0198, 0208 and 0233).
//!
//! The suite drives the walk itself — the reply, the proposal the table makes
//! of it, the policy decision, the browser's answer — so what is asserted is
//! the table's verdict and not a counter read in isolation.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{DispatchId, MonotonicMillis, PageEpoch, SemanticNodeId, TabId};
use bip_types::ActionResultCode;
use task_engine::action::ActionOutcome;
use task_engine::agent::{ModelToolCall, TurnPage};
use task_engine::{
    field_value_request_id_for_call, ArgumentValue, Authorization, BudgetKind, CapabilityId,
    Command, FieldValueAskOutcome, ManualClock, ModelHandle, ModelStopReason, ProposalDecision,
    ProviderRouteId, Reducer, SequentialIds, SuppliedArgument, SuppliedValueCount, TaskBudgets,
    TaskSeed, TaskState, TurnResidency, MAX_TURNS_ATTEMPTING_NOTHING, MAX_TURNS_WITHOUT_PROGRESS,
};

use common::agent::{page_on, read_call, reply, Digest};

/// An errand asked from the page on `tab_1`, admitting the moves this suite
/// makes on it.
fn seed() -> TaskSeed {
    let mut seed = common::errand::errand_seed();
    for tool in [
        "browser.dom.click",
        "browser.link.open",
        "user.request_values",
    ] {
        seed.snapshot.tool_allowlist.push(tool.to_owned());
    }
    seed
}

/// [`seed`], started from its page with one new site allowed, and running.
///
/// Every call here names the page's own node or reads the page, so each one
/// acts on `tab_1`, the source the task was asked from. So the blank tab the
/// consent prepares is never recorded: once a task has a tab of its own, the
/// person's page is read-only to it, and a press or a link on it is refused
/// before it is attempted (decision 0237). This suite's subject is what counts
/// as progress, not where a press may land, so it runs in the one state where
/// a press on the person's page is still made.
fn errand() -> common::Fixture {
    let mut fixture = common::draft_from(seed());
    let mut preview = common::preview();
    preview.source_discovery_enabled = true;
    preview.new_source_cap = 1;
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    preview.budgets = TaskBudgets::none()
        .with(BudgetKind::MaxSources, 2)
        .with(BudgetKind::MaxModelRequests, 64);
    fixture.must_apply(Command::StartTask(preview));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

fn click(node: u32) -> ModelToolCall {
    ModelToolCall::new(
        "browser.dom.click",
        vec![SuppliedArgument::new("node", ArgumentValue::Handle(node))],
    )
}

fn link_open(handle: ModelHandle) -> ModelToolCall {
    ModelToolCall::new(
        "browser.link.open",
        vec![SuppliedArgument::new(
            "node",
            ArgumentValue::Handle(handle.value()),
        )],
    )
}

/// One recorded turn composed from `page` whose one call is `call`.
fn turn(fixture: &mut common::Fixture, page: TurnPage, call: ModelToolCall) -> TurnResidency {
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let residency =
        TurnResidency::read(call_id, page, reply(ModelStopReason::ToolCall, vec![call]))
            .unwrap_or_else(|| unreachable!("the fixture page is explicitly readable"));
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    fixture.must_apply(Command::RecordModelTurn {
        call_id: residency.call_id().clone(),
        digest: Box::new(residency.digest(&dispositions)),
    });
    residency
}

/// The browser verified the action; a reading also says which document it
/// read, and it is always `epoch`.
fn verified(dispatch: DispatchId, reading: bool, epoch: &str) -> ActionOutcome {
    let mut outcome = common::verified_outcome();
    outcome.dispatch_id = Some(dispatch);
    match outcome.observation.as_mut() {
        Some(observation) if reading => observation.page_epoch = PageEpoch::new(epoch),
        _ => outcome.observation = None,
    }
    outcome
}

/// Walks `residency` the way the loop does: every proposal the table makes is
/// authorized, dispatched and verified on `epoch`, until the table answers
/// something else, which is returned.
fn walk(fixture: &mut common::Fixture, residency: &TurnResidency, epoch: &str) -> Command {
    loop {
        let next = fixture
            .reducer
            .next_agent_command(Some(residency), &Digest)
            .unwrap_or_else(|error| panic!("{error:?}"))
            .expect("a running errand always has a next command");
        let Command::ProposeAction(proposal) = next else {
            return next;
        };
        let key = proposal.idempotency_key.clone();
        let reading = proposal.tool_name() == common::READ_TOOL;
        fixture.must_apply(Command::ProposeAction(proposal));
        let action_id = fixture
            .reducer
            .actions()
            .find(|action| action.proposal().idempotency_key == key)
            .map(|action| action.action_id().clone())
            .expect("the proposal minted one action");
        let dispatch = DispatchId::new(format!("dispatch-{}", key.as_str()));
        fixture.must_apply(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: CapabilityId::new(format!("capability-{}", key.as_str())),
            })),
            dispatch_id: Some(dispatch.clone()),
        });
        fixture.must_apply(Command::RecordActionOutcome {
            action_id,
            outcome: Box::new(verified(dispatch, reading, epoch)),
        });
    }
}

/// One turn of the loop the phone measured: an even turn reads the page and
/// an odd one names a number nobody printed.
fn alternating_turn(fixture: &mut common::Fixture, turn_index: u8) -> (TurnResidency, Command) {
    let (page, handle) = page_on("epoch_1");
    let call = if turn_index.is_multiple_of(2) {
        read_call(handle)
    } else {
        click(handle.value().wrapping_add(4_242))
    };
    let residency = turn(fixture, page, call);
    let next = walk(fixture, &residency, "epoch_1");
    (residency, next)
}

fn rebuilt(fixture: &common::Fixture) -> common::TestReducer {
    let (reducer, _) = Reducer::replay(
        seed(),
        common::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        fixture.reducer.journal(),
    )
    .unwrap_or_else(|error| panic!("{error:?}"));
    reducer
}

/// The measured loop ends at the bound, with the ending its own facts name —
/// it read a page, so it is partly done — and not one paid turn later.
#[test]
fn a_reading_and_a_refused_call_in_turn_end_the_errand_at_the_bound() {
    let mut fixture = errand();
    for turn_index in 0..MAX_TURNS_WITHOUT_PROGRESS {
        let (_, next) = alternating_turn(&mut fixture, turn_index);
        // Row 21j is blind to this shape by construction: the reading
        // attempted something, so its run never passes one.
        assert!(fixture.reducer.turns_attempting_nothing() < MAX_TURNS_ATTEMPTING_NOTHING);
        if turn_index + 1 < MAX_TURNS_WITHOUT_PROGRESS {
            assert!(
                matches!(next, Command::RequestModelTurn { .. }),
                "turn {turn_index} is inside the bound and is asked again, got {next:?}"
            );
        } else {
            assert_eq!(next, Command::ResultCandidateReady);
        }
    }
    fixture.must_apply(Command::ResultCandidateReady);
    let next = fixture.reducer.next_agent_command(None, &Digest).unwrap();
    let Some(Command::PartialResultValidated(result)) = next else {
        panic!("an errand that read a page and did nothing is partly done: {next:?}");
    };
    assert_eq!(result.unmet[0].subject, "errand outcome");
    fixture.must_apply(Command::PartialResultValidated(result));
    assert_eq!(fixture.state(), TaskState::Partial);
}

/// Reading an unchanged page again is not progress, however many times.
#[test]
fn reading_the_same_page_again_is_not_progress() {
    let mut fixture = errand();
    let mut last = None;
    for _ in 0..MAX_TURNS_WITHOUT_PROGRESS {
        let (page, handle) = page_on("epoch_1");
        let residency = turn(&mut fixture, page, read_call(handle));
        last = Some(walk(&mut fixture, &residency, "epoch_1"));
        assert!(!fixture.reducer.progressed_this_turn());
    }
    assert_eq!(last, Some(Command::ResultCandidateReady));
}

/// A rebuilt task reaches the same count and the same verdict, at every turn
/// of the run and at the bound.
#[test]
fn a_rebuilt_errand_reaches_the_same_verdict() {
    let mut fixture = errand();
    for turn_index in 0..MAX_TURNS_WITHOUT_PROGRESS {
        let (residency, live) = alternating_turn(&mut fixture, turn_index);
        let restored = rebuilt(&fixture);
        assert_eq!(
            restored.turns_without_progress(),
            fixture.reducer.turns_without_progress()
        );
        assert_eq!(
            restored.progressed_this_turn(),
            fixture.reducer.progressed_this_turn()
        );
        assert_eq!(restored.errand_stalled(), fixture.reducer.errand_stalled());
        let replayed = restored
            .next_agent_command(Some(&residency), &Digest)
            .unwrap_or_else(|error| panic!("{error:?}"));
        assert_eq!(replayed, Some(live), "turn {turn_index}");
    }
    assert!(rebuilt(&fixture).errand_stalled());
}

/// The walk an errand is for: each page of one site read once and left by a
/// link it offered. Twice the bound in turns, and never near it.
#[test]
fn a_walk_across_the_pages_of_one_site_is_not_stalled() {
    let mut fixture = errand();
    for page_index in 1..=MAX_TURNS_WITHOUT_PROGRESS {
        let epoch = format!("epoch_{page_index}");
        for reading in [true, false] {
            let (page, handle) = page_on(&epoch);
            let call = if reading {
                read_call(handle)
            } else {
                link_open(handle)
            };
            let residency = turn(&mut fixture, page, call);
            let next = walk(&mut fixture, &residency, &epoch);
            assert!(
                matches!(next, Command::RequestModelTurn { .. }),
                "page {page_index} must go on, got {next:?}"
            );
            assert!(fixture.reducer.turns_without_progress() <= 1);
        }
    }
    let restored = rebuilt(&fixture);
    assert_eq!(
        restored.turns_without_progress(),
        fixture.reducer.turns_without_progress()
    );
}

/// There is room to look: a run one short of the bound, then a move, and the
/// count starts again from nothing.
#[test]
fn looking_up_to_the_bound_and_then_moving_starts_the_count_again() {
    let mut fixture = errand();
    for _ in 1..MAX_TURNS_WITHOUT_PROGRESS {
        let (page, handle) = page_on("epoch_1");
        let residency = turn(&mut fixture, page, read_call(handle));
        let next = walk(&mut fixture, &residency, "epoch_1");
        assert!(matches!(next, Command::RequestModelTurn { .. }), "{next:?}");
    }
    let (page, handle) = page_on("epoch_1");
    let residency = turn(&mut fixture, page, link_open(handle));
    let next = walk(&mut fixture, &residency, "epoch_1");
    assert!(matches!(next, Command::RequestModelTurn { .. }), "{next:?}");
    assert!(fixture.reducer.progressed_this_turn());
    let (page, handle) = page_on("epoch_2");
    let residency = turn(&mut fixture, page, read_call(handle));
    walk(&mut fixture, &residency, "epoch_2");
    assert_eq!(fixture.reducer.turns_without_progress(), 0);
}

/// The same control pressed again, on the same page, is not progress: the
/// browser verified it both times and nothing is different after the second.
#[test]
fn pressing_the_same_control_again_is_not_progress() {
    let mut fixture = errand();
    let (page, handle) = page_on("epoch_1");
    let residency = turn(&mut fixture, page, click(handle.value()));
    walk(&mut fixture, &residency, "epoch_1");
    assert!(fixture.reducer.progressed_this_turn(), "the first press is");
    let mut last = None;
    for _ in 0..MAX_TURNS_WITHOUT_PROGRESS {
        let (page, handle) = page_on("epoch_1");
        let residency = turn(&mut fixture, page, click(handle.value()));
        last = Some(walk(&mut fixture, &residency, "epoch_1"));
        assert!(!fixture.reducer.progressed_this_turn());
    }
    assert_eq!(last, Some(Command::ResultCandidateReady));
}

/// A value from the person clears the run; an ask that came back empty is one
/// more turn of it, and at the bound it is the one that ends the errand —
/// long before row 12's own bound on empty asks would have.
#[test]
fn a_value_from_the_person_is_progress_and_an_empty_ask_is_not() {
    for supplied in [1, 0] {
        let mut fixture = errand();
        for _ in 1..MAX_TURNS_WITHOUT_PROGRESS {
            let (page, handle) = page_on("epoch_1");
            let residency = turn(&mut fixture, page, read_call(handle));
            walk(&mut fixture, &residency, "epoch_1");
        }
        let (page, _) = page_on("epoch_1");
        let residency = turn(
            &mut fixture,
            page,
            ModelToolCall::new("user.request_values", vec![]),
        );
        let ordinal = fixture
            .reducer
            .model_turn()
            .map_or(0, task_engine::ModelTurn::ordinal);
        let request_id = field_value_request_id_for_call(ordinal, 0);
        fixture.must_apply(Command::RequestFieldValues {
            request_id: request_id.clone(),
            tab_id: TabId::new("tab_1"),
            node_id: SemanticNodeId::new("n-1"),
            companion_node_ids: task_engine::FieldNodeIds::none(),
        });
        fixture.must_apply(Command::SupplyFieldValues {
            request_id,
            supplied: SuppliedValueCount::new(supplied).expect("within the bound"),
            outcome: Some(FieldValueAskOutcome::ChallengeOffScreen),
            field_node_ids: None,
        });
        let next = fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap_or_else(|error| panic!("{error:?}"));
        if supplied == 0 {
            assert_eq!(next, Some(Command::ResultCandidateReady));
            assert_eq!(fixture.reducer.unanswered_value_asks(), 1);
        } else {
            assert!(
                matches!(next, Some(Command::RequestModelTurn { .. })),
                "{next:?}"
            );
        }
    }
}

/// A move the browser refused is not progress: the link was gone, nothing was
/// opened, and the page is the page it was.
#[test]
fn a_move_the_browser_refused_is_not_progress() {
    let mut fixture = errand();
    let (page, handle) = page_on("epoch_1");
    let residency = turn(&mut fixture, page, link_open(handle));
    let Some(Command::ProposeAction(proposal)) = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap()
    else {
        panic!("the link is proposed");
    };
    let key = proposal.idempotency_key.clone();
    fixture.must_apply(Command::ProposeAction(proposal));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key == key)
        .map(|action| action.action_id().clone())
        .unwrap();
    let dispatch = DispatchId::new("dispatch-refused");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(common::authorize()),
        dispatch_id: Some(dispatch.clone()),
    });
    fixture.must_apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::NodeGone,
            dispatch_id: Some(dispatch),
            observed_at: MonotonicMillis(10),
            observation: None,
            discovered_source: None,
        }),
    });
    assert!(!fixture.reducer.progressed_this_turn());
}
