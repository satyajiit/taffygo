// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the walk does with a bootstrap read that came back refused.
//!
//! It used to do one thing for all of them: any terminal state that was not
//! `Verified` made the source `Refused`, and the walk answers that with
//! `FailTask(SourcesUnavailable)` — "Taffy could not read enough to answer",
//! from one refusal, with nothing retried. The recovery ladder already says
//! which refusals a runtime may answer by itself, and for a read the answer it
//! gives is to read the page again. Decision 0164.

mod common;

use bip_types::identity::{ActionId, DispatchId, FrameId, PageEpoch};
use bip_types::{ActionResultCode, Sensitivity};
use task_engine::action::ActionOutcome;
use task_engine::observation::{
    ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence,
};
use task_engine::task::FailureReason;
use task_engine::{
    Authorization, BudgetKind, CapabilityId, Command, PreModelObservation, ProposalDecision,
    ProviderRouteId, TaskBudgets, TaskTemplateId, REVIEWED_OBSERVATION_TOOL,
};

use common::agent::Digest;

fn errand_with_one_source() -> common::Fixture {
    let mut seed = common::seed();
    seed.snapshot.template_id = TaskTemplateId::WebErrand;
    seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    seed.snapshot.tool_allowlist = vec![
        REVIEWED_OBSERVATION_TOOL.to_owned(),
        "browser.reload".to_owned(),
    ];
    let mut fixture = common::draft_from(seed);
    let mut preview = common::preview();
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    preview.source_discovery_enabled = true;
    preview.new_source_cap = 4;
    preview.budgets = TaskBudgets::none().with(BudgetKind::MaxSources, 5);
    fixture.must_apply(Command::StartTask(preview));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

/// Drives the walk's next bootstrap read to a refusal carrying `code`, and
/// answers what the walk asks for after it.
fn read_then_refuse(fixture: &mut common::Fixture, code: ActionResultCode, ordinal: u32) {
    let PreModelObservation::Command(Command::ProposeAction(proposal)) = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("the errand may read: {error:?}"))
    else {
        unreachable!("a source with no reading needs one")
    };
    let key = proposal.idempotency_key.as_str().to_owned();
    fixture.must_apply(Command::ProposeAction(proposal));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == key)
        .map_or_else(
            || unreachable!("the proposal mints one action"),
            |action| action.action_id().clone(),
        );
    let dispatch = DispatchId::new(format!("dispatch-read-{ordinal}"));
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new(format!("capability-read-{ordinal}")),
        })),
        dispatch_id: Some(dispatch.clone()),
    });
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action_id.as_str().to_owned()),
        outcome: Box::new(ActionOutcome {
            code,
            dispatch_id: Some(dispatch),
            observed_at: bip_types::identity::MonotonicMillis(10),
            observation: None,
            discovered_source: None,
        }),
    });
}

/// A complete reading of the errand's tab. Never handed to the walk as a live
/// observation, so it verifies and is never the one the source needs.
fn reading(ordinal: u32) -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: task_engine::deps::TabId::new("tab_1"),
        frame_id: FrameId("frame_1".to_owned()),
        page_epoch: PageEpoch(format!("epoch_{ordinal}")),
        graph_revision: u64::from(ordinal),
        normalized_origin: "https://example.test".to_owned(),
        private_profile: false,
        completeness: ObservationCompleteness::Complete,
        graph: ObservationGraphSummary {
            node_count: 1,
            relationship_count: 0,
            named_node_count: 1,
            text_run_count: 1,
            text_byte_count: 4,
        },
        total_bytes: 32,
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::NotSensitive,
    }
}

/// The case that ended an errand on the phone. A site redirected within its own
/// registrable domain between the grant and the read, the browser refused the
/// read, and one refusal was the whole verdict.
#[test]
fn a_read_refused_for_a_reason_a_fresh_look_answers_is_read_again() {
    let mut fixture = errand_with_one_source();
    read_then_refuse(&mut fixture, ActionResultCode::StalePageEpoch, 1);
    let next = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("{error:?}"));
    assert!(
        matches!(
            next,
            PreModelObservation::Command(Command::ProposeAction(_))
        ),
        "a stale reading asks for another, not for the task to end: {next:?}"
    );
}

/// And the bound, which is the refusal ledger's and not a second copy of it.
/// A reading refused the same way over and over ends the source, and the bound
/// is this crate's own [`task_engine::MAX_SOURCE_BOOTSTRAP_READS`].
///
/// It is asserted exactly rather than as a ceiling, because it used to be a
/// ceiling nothing reached. The repetition register was asked as well and
/// answered first — it abandons a call at three and this bound is four — so a
/// source got three attempts while both the constant and the decision that set
/// it said four (decision 0196).
#[test]
fn a_source_whose_reading_is_abandoned_is_a_source_this_task_cannot_read() {
    let mut fixture = errand_with_one_source();
    let mut reads = 0u32;
    loop {
        let next = fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("{error:?}"));
        if !matches!(
            next,
            PreModelObservation::Command(Command::ProposeAction(_))
        ) {
            assert_eq!(
                next,
                PreModelObservation::Command(Command::FailTask {
                    reason: FailureReason::SourcesUnavailable,
                })
            );
            break;
        }
        reads += 1;
        assert!(
            reads <= task_engine::MAX_SOURCE_BOOTSTRAP_READS,
            "a refused reading may not be asked for without bound"
        );
        read_then_refuse(&mut fixture, ActionResultCode::StalePageEpoch, reads);
    }
    assert_eq!(
        reads,
        task_engine::MAX_SOURCE_BOOTSTRAP_READS,
        "the source gets every read its own bound allows"
    );
}

/// And the register the model's calls are counted in never sees them.
///
/// The walk's read fingerprints as `(browser.dom.read, tab, no node)`, which is
/// the same number a model's read of the same tab produces. So the two authors
/// shared one counter: the walk's refusals spent the model's allowance, and the
/// model's spent the walk's. Whatever the bounds are worth, they are worth
/// nothing shared (decision 0196).
#[test]
fn the_walks_own_reading_is_not_counted_against_the_model() {
    let mut fixture = errand_with_one_source();
    for ordinal in 1..=task_engine::MAX_SOURCE_BOOTSTRAP_READS {
        read_then_refuse(&mut fixture, ActionResultCode::Unsupported, ordinal);
    }
    assert_eq!(
        fixture.reducer.refusals().total(),
        0,
        "a read the walk proposed for itself is not one of the model's calls"
    );
}

/// And a refusal the recovery ladder will not retry is still one attempt.
///
/// It used to end the task on the first one, on the argument that there is
/// nothing to look at again. A phone disagreed: a followed link landed on the
/// official site, the page had not finished becoming observable, the reading
/// came back `kUnsupported` with no nodes at all, and the errand reported that
/// it could not read enough to answer — one proposal after arriving exactly
/// where it meant to. "Nothing can change this" was a claim about a document
/// that changed a second later (decision 0181).
///
/// The bound is what says a source is unreadable, and it says it about the
/// source rather than about one reading of it.
#[test]
fn a_read_refused_for_a_reason_nothing_can_change_is_still_asked_again() {
    let mut fixture = errand_with_one_source();
    read_then_refuse(&mut fixture, ActionResultCode::Unsupported, 1);
    assert!(
        matches!(
            fixture
                .reducer
                .next_pre_model_observation(&[], &Digest)
                .unwrap_or_else(|error| unreachable!("{error:?}")),
            PreModelObservation::Command(Command::ProposeAction(_)),
        ),
        "a page that was not ready yet is asked for again",
    );
    let mut reads = 1u32;
    loop {
        let next = fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("{error:?}"));
        if !matches!(
            next,
            PreModelObservation::Command(Command::ProposeAction(_))
        ) {
            assert_eq!(
                next,
                PreModelObservation::Command(Command::FailTask {
                    reason: FailureReason::SourcesUnavailable,
                }),
                "and a page that is never ready ends the task",
            );
            break;
        }
        reads += 1;
        assert!(
            reads <= task_engine::MAX_SOURCE_BOOTSTRAP_READS,
            "a refused reading may not be asked for without bound"
        );
        read_then_refuse(&mut fixture, ActionResultCode::Unsupported, reads);
    }
    assert_eq!(
        reads,
        task_engine::MAX_SOURCE_BOOTSTRAP_READS,
        "the page that was not ready gets every read its bound allows"
    );
}

/// The bound is about the document, and an errand walks one tab through many.
///
/// A phone ran an errand whose every move verified — search, read, follow a
/// result, read, query, navigate, read, search, read, follow a result — and it
/// ended `FailTask(SourcesUnavailable)` on the commit *after* the last
/// `browser.link.open` verified, with no reading of the page it had just
/// arrived on ever attempted. Four bootstrap readings had been spent in that
/// tab across the sites before it, and the bound counted the tab rather than
/// the document (decision 0181).
#[test]
fn arriving_somewhere_new_starts_the_reading_count_again() {
    let mut fixture = errand_with_one_source();
    // Verified readings, none of them live: this is run AZ's own shape, where
    // every move the errand made verified and the count still reached the
    // bound. A refused one would reach it too and would additionally be
    // abandoned by the refusal ledger, which is a different latch.
    for ordinal in 1..=task_engine::MAX_SOURCE_BOOTSTRAP_READS {
        read_then_verify(&mut fixture, ordinal);
    }
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("{error:?}")),
        PreModelObservation::Command(Command::FailTask {
            reason: FailureReason::SourcesUnavailable,
        }),
        "the bound still ends a source that never reads",
    );

    // The tab goes somewhere else, and the move verifies.
    verified_move(&mut fixture, 9);
    assert!(
        matches!(
            fixture
                .reducer
                .next_pre_model_observation(&[], &Digest)
                .unwrap_or_else(|error| unreachable!("{error:?}")),
            PreModelObservation::Command(Command::ProposeAction(_)),
        ),
        "a page the task has just arrived on is read, not refused unread",
    );
}

/// One verified `browser.reload` of the source's tab, which is the smallest
/// move in the set that lands the tab on a document again.
fn verified_move(fixture: &mut common::Fixture, ordinal: u32) {
    let tab = fixture
        .reducer
        .task()
        .consented_sources()
        .first()
        .map_or_else(
            || unreachable!("the errand holds one source"),
            |source| source.tab_id.clone(),
        );
    let proposal = task_engine::action::ActionProposal::new(
        task_engine::action::ActionIntent::Browser(task_engine::action::BrowserIntent::Reload {
            tab,
        }),
        None,
        task_engine::IdempotencyKey::new(format!("turn-{ordinal}-call-0")),
        false,
        None,
        bip_types::identity::ContentDigest {
            algorithm: bip_types::identity::DigestAlgorithm::Sha256,
            value: "9".repeat(64),
        },
    );
    let key = proposal.idempotency_key.as_str().to_owned();
    fixture.must_apply(Command::ProposeAction(Box::new(proposal)));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == key)
        .map_or_else(
            || unreachable!("the proposal mints one action"),
            |action| action.action_id().clone(),
        );
    let dispatch = DispatchId::new(format!("dispatch-move-{ordinal}"));
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new(format!("capability-move-{ordinal}")),
        })),
        dispatch_id: Some(dispatch.clone()),
    });
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action_id.as_str().to_owned()),
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::Verified,
            dispatch_id: Some(dispatch),
            observed_at: bip_types::identity::MonotonicMillis(20),
            observation: None,
            discovered_source: None,
        }),
    });
}

/// Drives the walk's next bootstrap read to a verified outcome whose reading
/// the arena does not hold, which is the state a settled navigation leaves.
fn read_then_verify(fixture: &mut common::Fixture, ordinal: u32) {
    let PreModelObservation::Command(Command::ProposeAction(proposal)) = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("the errand may read: {error:?}"))
    else {
        unreachable!("a source with no reading needs one")
    };
    let key = proposal.idempotency_key.as_str().to_owned();
    fixture.must_apply(Command::ProposeAction(proposal));
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == key)
        .map_or_else(
            || unreachable!("the proposal mints one action"),
            |action| action.action_id().clone(),
        );
    let dispatch = DispatchId::new(format!("dispatch-verified-{ordinal}"));
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new(format!("capability-verified-{ordinal}")),
        })),
        dispatch_id: Some(dispatch.clone()),
    });
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action_id.as_str().to_owned()),
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::Verified,
            dispatch_id: Some(dispatch),
            observed_at: bip_types::identity::MonotonicMillis(10),
            observation: Some(reading(ordinal)),
            discovered_source: None,
        }),
    });
}

/// A reading that opened a paid turn is not evidence the source cannot be read.
///
/// The bound counted every reading of a source for the life of the task,
/// including the ones that worked, so an errand that did several things on one
/// page ran out of readings of it. A phone measured that on 2026-09-19: six
/// readings of the myAadhaar download page, nine model turns served from them,
/// three clicks and a query in between, and then one scroll dropped the live
/// reading and the errand reported that it could not read enough to answer
/// (decision 0197).
#[test]
fn a_reading_that_opened_a_turn_is_spent_and_not_held_against_the_source() {
    let mut fixture = errand_with_one_source();
    // Three times what the bound allows, each one used.
    for ordinal in 1..=(task_engine::MAX_SOURCE_BOOTSTRAP_READS * 3) {
        read_then_verify(&mut fixture, ordinal);
        let call_id = fixture.reducer.next_model_call_id();
        fixture.must_apply(Command::RequestModelTurn {
            call_id: call_id.clone(),
        });
        fixture.must_apply(Command::RecordModelTurn {
            call_id,
            digest: Box::new(common::turn_digest()),
        });
    }
    let next = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("{error:?}"));
    assert!(
        matches!(
            next,
            PreModelObservation::Command(Command::ProposeAction(_))
        ),
        "a page read and used a dozen times is a page this task can read: {next:?}"
    );
}

/// And the bound still ends a source, which is the half an unordered register
/// can silently lose.
///
/// `Reducer::actions()` walks a `BTreeMap` keyed by identifier, so it yields
/// `act_10` before `act_2`: past ten actions, nothing downstream can tell
/// which of two records came first. A reset computed inside that walk is
/// therefore applied in the wrong places — here it would zero four readings
/// that came *after* the last move, because eight of the moves sort after them
/// — and the source that cannot be read reads as one that can. The reducer
/// marks a superseded record at the move instead, which is a fact recorded in
/// order rather than one recovered from an order that is not there.
#[test]
fn the_bound_survives_a_register_that_is_not_in_time_order() {
    let mut fixture = errand_with_one_source();
    // Ten moves and not one reading, so the readings below are act_10 and up
    // while every move is act_0 through act_9.
    for ordinal in 0..10 {
        verified_move(&mut fixture, ordinal);
    }
    for ordinal in 1..=task_engine::MAX_SOURCE_BOOTSTRAP_READS {
        read_then_verify(&mut fixture, ordinal);
    }
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("{error:?}")),
        PreModelObservation::Command(Command::FailTask {
            reason: FailureReason::SourcesUnavailable,
        }),
        "four readings the walk cannot use end the source however they are \
         stored",
    );
}
