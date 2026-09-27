// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The page read that must precede every paid turn over an empty live arena.

mod common;

use bip_types::identity::{ActionId, DispatchId, FrameId, MonotonicMillis, PageEpoch};
use bip_types::{ActionResultCode, Sensitivity};
use task_engine::action::{ActionIntent, ActionOutcome, BrowserIntent};
use task_engine::{
    Authorization, CapabilityId, Command, Denial, LiveSourceObservation, ObservationCompleteness,
    ObservationGraphSummary, PageObservationEvidence, PreModelObservation, ProposalDecision,
    ProviderRouteId, TaskTemplateId, REVIEWED_OBSERVATION_TOOL,
};

use common::agent::Digest;

fn running_direct_task() -> common::Fixture {
    let mut seed = common::seed();
    seed.snapshot.template_id = TaskTemplateId::BuildSourceTable;
    seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    seed.snapshot.tool_allowlist = vec![REVIEWED_OBSERVATION_TOOL.to_owned()];
    let mut fixture = common::draft_from(seed);
    let mut preview = common::preview();
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    fixture.must_apply(Command::StartTask(preview));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

fn running_comparison_task() -> common::Fixture {
    running_source_task(2)
}

fn running_source_task(count: u8) -> common::Fixture {
    let mut seed = common::seed();
    seed.snapshot.template_id = TaskTemplateId::CompareProducts;
    seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    seed.snapshot.tool_allowlist = vec![REVIEWED_OBSERVATION_TOOL.to_owned()];
    let mut fixture = common::draft_from(seed);
    let mut preview = common::preview();
    for source in 2..=count {
        let source_id = common::source_id(source);
        preview.scope = preview.scope.include(source_id);
        preview.sources.push(task_engine::ConsentedSource {
            source_id,
            tab_id: task_engine::deps::TabId::new(format!("tab_{source}")),
            normalized_origin: format!("https://source-{source}.example"),
            canonical_locator: None,
        });
    }
    preview.budgets = task_engine::TaskBudgets::none()
        .with(task_engine::BudgetKind::MaxSources, u64::from(count));
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    fixture.must_apply(Command::StartTask(preview));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}

#[test]
fn an_empty_live_page_proposes_one_exact_whole_document_read_before_a_paid_turn() {
    let fixture = running_direct_task();
    let first = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("the one-source task is valid: {error:?}"));
    let again = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("the same revision is valid: {error:?}"));
    assert_eq!(first, again, "one durable revision has one proposal");

    let task_engine::PreModelObservation::Command(Command::ProposeAction(proposal)) = first else {
        unreachable!("the empty page is observed before any paid turn")
    };
    assert_eq!(
        proposal.intent(),
        &ActionIntent::Browser(BrowserIntent::DomRead {
            tab: task_engine::deps::TabId::new("tab_1"),
            target: None,
        })
    );
    assert!(proposal
        .idempotency_key
        .as_str()
        .starts_with("agent-observation-"));
}

fn proposed(fixture: &mut common::Fixture) -> (task_engine::deps::ActionId, String) {
    let PreModelObservation::Command(command @ Command::ProposeAction(_)) = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("the observation is available: {error:?}"))
    else {
        unreachable!("the empty page proposes a read")
    };
    let key = match &command {
        Command::ProposeAction(proposal) => proposal.idempotency_key.as_str().to_owned(),
        _ => unreachable!("matched above"),
    };
    fixture.must_apply(command);
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == key)
        .map_or_else(
            || unreachable!("the proposal minted an action"),
            |action| action.action_id().clone(),
        );
    (action_id, key)
}

#[test]
fn a_read_in_policy_or_dispatch_waits_and_never_opens_the_model_path() {
    let mut fixture = running_direct_task();
    let (action_id, _) = proposed(&mut fixture);
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("pending is a valid state: {error:?}")),
        PreModelObservation::Waiting
    );
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id,
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new("capability-observe"),
        })),
        dispatch_id: Some(DispatchId::new("dispatch-observe")),
    });
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("dispatch is a valid state: {error:?}")),
        PreModelObservation::Waiting
    );
}

/// One refused reading is an attempt; the bound is the verdict (decision 0181).
///
/// This used to end the task on the first refusal, and a phone showed what
/// that costs: a followed link landed on the official site, the page had not
/// finished becoming observable, the reading came back `kUnsupported` with no
/// nodes, and the errand reported that it could not read enough to answer one
/// proposal after arriving where it meant to.
#[test]
fn a_refused_reading_is_one_attempt_and_the_bound_is_the_verdict() {
    let mut fixture = running_direct_task();
    for attempt in 1..=task_engine::MAX_SOURCE_BOOTSTRAP_READS {
        let (action_id, _) = proposed(&mut fixture);
        fixture.must_apply(Command::RecordPolicyDecision {
            action_id,
            decision: Box::new(ProposalDecision::Deny(Denial::new(
                ActionResultCode::DeniedByPolicy,
            ))),
            dispatch_id: None,
        });
        let next = fixture
            .reducer
            .next_pre_model_observation(&[], &Digest)
            .unwrap_or_else(|error| unreachable!("rejection settles locally: {error:?}"));
        if attempt < task_engine::MAX_SOURCE_BOOTSTRAP_READS {
            assert!(
                matches!(
                    next,
                    PreModelObservation::Command(Command::ProposeAction(_))
                ),
                "attempt {attempt} must ask again rather than end the task",
            );
        } else {
            assert_eq!(
                next,
                PreModelObservation::Command(Command::FailTask {
                    reason: task_engine::FailureReason::SourcesUnavailable,
                }),
                "a source this many attempts could not read is unreadable, honestly",
            );
        }
    }
}

#[test]
fn a_restored_empty_arena_reobserves_under_the_new_durable_revision() {
    let mut fixture = running_direct_task();
    let (action_id, first_key) = proposed(&mut fixture);
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new("capability-observe"),
        })),
        dispatch_id: Some(DispatchId::new("dispatch-observe")),
    });
    let observed = observation("tab_1", "https://example.test", "epoch_1");
    fixture.must_apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::Verified,
            dispatch_id: Some(DispatchId::new("dispatch-observe")),
            observed_at: MonotonicMillis(2_000),
            observation: Some(observed.clone()),
            discovered_source: None,
        }),
    });
    let mut different_epoch = observed.clone();
    different_epoch.page_epoch = PageEpoch("epoch_2".to_owned());
    assert!(matches!(
        fixture
            .reducer
            .next_pre_model_observation(&[live(1, different_epoch)], &Digest),
        Ok(PreModelObservation::Command(Command::ProposeAction(_)))
    ));
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(&[live(1, observed)], &Digest)
            .unwrap_or_else(|error| unreachable!("verified live page is ready: {error:?}")),
        PreModelObservation::Ready
    );
    let PreModelObservation::Command(Command::ProposeAction(second)) = fixture
        .reducer
        .next_pre_model_observation(&[], &Digest)
        .unwrap_or_else(|error| unreachable!("restored empty arena reobserves: {error:?}"))
    else {
        unreachable!("a restore lost the transient page and needs a new read")
    };
    assert_ne!(first_key, second.idempotency_key.as_str());
}

fn observation(tab: &str, origin: &str, epoch: &str) -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: task_engine::deps::TabId::new(tab),
        frame_id: FrameId("frame_1".to_owned()),
        page_epoch: PageEpoch(epoch.to_owned()),
        graph_revision: 7,
        normalized_origin: origin.to_owned(),
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

fn live(source: u8, evidence: PageObservationEvidence) -> LiveSourceObservation {
    LiveSourceObservation {
        source_id: common::source_id(source),
        evidence,
    }
}

fn verify(
    fixture: &mut common::Fixture,
    action_id: ActionId,
    dispatch: &str,
    evidence: PageObservationEvidence,
) {
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(ProposalDecision::Authorize(Authorization {
            capability_id: CapabilityId::new(format!("capability-{dispatch}")),
        })),
        dispatch_id: Some(DispatchId::new(dispatch)),
    });
    fixture.must_apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(ActionOutcome {
            code: ActionResultCode::Verified,
            dispatch_id: Some(DispatchId::new(dispatch)),
            observed_at: MonotonicMillis(2_000),
            observation: Some(evidence),
            discovered_source: None,
        }),
    });
}

#[test]
fn comparison_observes_every_source_in_stable_order_before_the_paid_turn() {
    let mut fixture = running_comparison_task();
    let (first_action, _) = proposed(&mut fixture);
    let first_intent = fixture
        .reducer
        .actions()
        .find(|action| action.action_id() == &first_action)
        .map(|action| action.proposal().intent().clone());
    assert!(matches!(
        first_intent,
        Some(ActionIntent::Browser(BrowserIntent::DomRead { tab, target: None }))
            if tab.as_str() == "tab_1"
    ));
    let first_evidence = observation("tab_1", "https://example.test", "epoch_1");
    verify(
        &mut fixture,
        first_action,
        "dispatch-first",
        first_evidence.clone(),
    );

    let first_live = [live(1, first_evidence.clone())];
    let PreModelObservation::Command(second @ Command::ProposeAction(_)) = fixture
        .reducer
        .next_pre_model_observation(&first_live, &Digest)
        .unwrap_or_else(|error| unreachable!("the second source is valid: {error:?}"))
    else {
        unreachable!("the second source must be read before the model")
    };
    fixture.must_apply(second);
    let second_action = fixture
        .reducer
        .actions()
        .find(|action| {
            matches!(
                action.proposal().intent(),
                ActionIntent::Browser(BrowserIntent::DomRead { tab, target: None })
                    if tab.as_str() == "tab_2"
            )
        })
        .map_or_else(
            || unreachable!("the second proposal names the second tab"),
            |action| action.action_id().clone(),
        );
    let second_evidence = observation("tab_2", "https://source-2.example", "epoch_2");
    verify(
        &mut fixture,
        second_action,
        "dispatch-second",
        second_evidence.clone(),
    );
    assert_eq!(
        fixture
            .reducer
            .next_pre_model_observation(
                &[live(1, first_evidence), live(2, second_evidence)],
                &Digest,
            )
            .unwrap_or_else(|error| unreachable!("both receipts are valid: {error:?}")),
        PreModelObservation::Ready
    );
}

#[test]
fn four_independent_sources_may_be_pending_and_a_completion_opens_one_slot() {
    let mut fixture = running_source_task(5);
    let pending: Vec<_> = (0..task_engine::MAX_PARALLEL_SOURCE_READS)
        .map(|_| proposed(&mut fixture).0)
        .collect();
    assert_eq!(pending.len(), 4);
    assert_eq!(
        fixture.reducer.next_pre_model_observation(&[], &Digest),
        Ok(PreModelObservation::Waiting),
        "a fifth source cannot exceed the independent-read bound"
    );
    let third = pending
        .get(2)
        .unwrap_or_else(|| unreachable!("four reads were proposed"))
        .clone();
    let third_evidence = observation("tab_3", "https://source-3.example", "epoch_3");
    verify(
        &mut fixture,
        third,
        "dispatch-third",
        third_evidence.clone(),
    );
    let live = [live(3, third_evidence)];
    let Ok(PreModelObservation::Command(command @ Command::ProposeAction(_))) =
        fixture.reducer.next_pre_model_observation(&live, &Digest)
    else {
        unreachable!("the completed third read frees a slot for source five")
    };
    if let Command::ProposeAction(proposal) = &command {
        assert!(matches!(
            proposal.intent(),
            ActionIntent::Browser(BrowserIntent::DomRead { tab, target: None })
                if tab.as_str() == "tab_5"
        ));
    }
    fixture.must_apply(command);
    assert_eq!(
        fixture.reducer.next_pre_model_observation(&live, &Digest),
        Ok(PreModelObservation::Waiting)
    );
}

#[test]
fn a_refusal_in_a_later_source_asks_again_rather_than_starting_the_fifth() {
    let mut fixture = running_source_task(5);
    let pending: Vec<_> = (0..4).map(|_| proposed(&mut fixture).0).collect();
    let second = pending.get(1).unwrap_or_else(|| unreachable!()).clone();
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: second,
        decision: Box::new(ProposalDecision::Deny(Denial::new(
            ActionResultCode::DeniedByPolicy,
        ))),
        dispatch_id: None,
    });
    // The refused source is `Needed` again, and it is chosen in source order
    // ahead of the fifth: three reads are still in flight, so the parallel cap
    // admits exactly one more and it is a retry rather than a new site.
    let Ok(PreModelObservation::Command(Command::ProposeAction(proposal))) =
        fixture.reducer.next_pre_model_observation(&[], &Digest)
    else {
        unreachable!("a refused reading asks again")
    };
    assert!(
        matches!(
            proposal.intent(),
            task_engine::action::ActionIntent::Browser(
                task_engine::action::BrowserIntent::DomRead { tab, target: None },
            ) if tab.as_str() == "tab_2",
        ),
        "the read that was refused is the one re-proposed, not a fifth source",
    );
}
