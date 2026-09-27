// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Finishing an errand is a claim the browser has verified, and narrating is
//! not (decision 0136).
//!
//! Row 13 of the agent table ends a research task on any prose reply. An
//! errand is different: a prose reply before any verified outcome is nudged
//! with a fresh turn, at most `MAX_UNPRODUCTIVE_ERRAND_REPLIES` times, and
//! then the task fails honestly. A handover and form preparation must resume
//! work. A started download must have a task-owned Complete snapshot before
//! a prose reply can offer a result.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{ActionId, TabId};
use task_engine::action::BrowserIntent;
use task_engine::{
    Command, TaskDownloadActionResult, TaskDownloadHandleTable, TaskDownloadSnapshot,
    TaskDownloadState, TaskKind, TaskState, TurnResidency,
};

use common::agent::{record_turn, request_turn, Digest};
use common::errand::{
    discovered_outcome, dispatch_authorized, prepared_errand, source, BROWSER_SESSION,
    DISCOVERY_TAB,
};

/// A prepared zero-source errand on its discovery tab.
fn errand() -> common::Fixture {
    prepared_errand(4)
}

fn prose_reply(fixture: &mut common::Fixture) {
    fixture.must_apply(request_turn(fixture));
    fixture.must_apply(record_turn(fixture));
}

#[test]
fn a_prose_reply_before_any_outcome_earns_a_fresh_turn_and_is_counted() {
    let mut fixture = errand();
    prose_reply(&mut fixture);
    assert_eq!(fixture.reducer.unproductive_replies(), 1);
    assert!(!fixture.reducer.errand_outcome_witnessed());
    let next = fixture
        .reducer
        .next_agent_command(None, &Digest)
        .unwrap_or_else(|error| panic!("{error:?}"));
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "the model is asked again with the nudge, not offered a result: {next:?}"
    );
}

#[test]
fn a_started_download_needs_its_own_complete_browser_snapshot_to_finish() {
    let mut fixture = errand();
    // The errand reaches its site first: a typed navigate on the discovery
    // tab binds the first source (decision 0136 section 3). A download from
    // a tab bound to no site would be a mismatch, as it should be.
    let (navigate, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::Navigate {
            tab: TabId::new(DISCOVERY_TAB),
            address: "https://myaadhaar.uidai.gov.in/".to_owned(),
            new_tab: false,
        },
        0,
    );
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(navigate),
        outcome: Box::new(discovered_outcome(
            dispatch,
            source(7, "https://myaadhaar.uidai.gov.in"),
        )),
    });
    let (action, dispatch) = dispatch_authorized(
        &mut fixture,
        BrowserIntent::DownloadStart {
            tab: TabId::new(DISCOVERY_TAB),
            address: "https://myaadhaar.uidai.gov.in/eaadhaar.pdf".to_owned(),
            browser_session_id: task_engine::BrowserSessionId::new(BROWSER_SESSION)
                .unwrap_or_else(|_| unreachable!()),
        },
        1,
    );
    let mut outcome = discovered_outcome(dispatch, source(8, "https://myaadhaar.uidai.gov.in"));
    outcome.discovered_source = None;
    fixture.must_apply(Command::RecordActionOutcome {
        action_id: ActionId::new(action),
        outcome: Box::new(outcome),
    });
    assert!(fixture.reducer.errand_outcome_witnessed());
    // The reply that follows is the errand's answer, not narration.
    prose_reply(&mut fixture);
    assert_eq!(fixture.reducer.unproductive_replies(), 0);
    assert!(matches!(
        fixture.reducer.next_agent_command(None, &Digest).unwrap(),
        Some(Command::RequestModelTurn { .. })
    ));
    for state in [
        TaskDownloadState::Created,
        TaskDownloadState::InProgress,
        TaskDownloadState::Paused,
        TaskDownloadState::Interrupted,
        TaskDownloadState::Cancelled,
    ] {
        let residency = download_residency(&fixture, state, true, BROWSER_SESSION);
        assert!(
            matches!(
                fixture
                    .reducer
                    .next_agent_command(Some(&residency), &Digest)
                    .unwrap(),
                Some(Command::RequestModelTurn { .. })
            ),
            "{state:?} is not complete"
        );
    }
    for (owned, session) in [(false, BROWSER_SESSION), (true, "another-session")] {
        let residency = download_residency(&fixture, TaskDownloadState::Complete, owned, session);
        assert!(
            matches!(
                fixture
                    .reducer
                    .next_agent_command(Some(&residency), &Digest)
                    .unwrap(),
                Some(Command::RequestModelTurn { .. })
            ),
            "another task or session cannot complete this download"
        );
    }
    let residency =
        download_residency(&fixture, TaskDownloadState::Complete, true, BROWSER_SESSION);
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap_or_else(|error| panic!("{error:?}")),
        Some(Command::ResultCandidateReady)
    );
}

fn download_residency(
    fixture: &common::Fixture,
    state: TaskDownloadState,
    owned: bool,
    session: &str,
) -> TurnResidency {
    let download = TaskDownloadSnapshot::new(
        "browser-download-1".to_owned(),
        state,
        task_engine::TaskDownloadMediaType::Application,
        4096,
        task_engine::TaskDownloadDirectoryClass::Undecided,
    )
    .unwrap();
    let session = task_engine::BrowserSessionId::new(session).unwrap();
    let result = if owned {
        TaskDownloadActionResult::started(session, download)
    } else {
        TaskDownloadActionResult::listed(session, vec![download], false).unwrap()
    };
    let mut downloads = TaskDownloadHandleTable::default();
    downloads.apply_verified(&result).unwrap();
    TurnResidency::read(
        fixture.reducer.model_turn().unwrap().call_id().clone(),
        common::agent::page().0,
        task_engine::ModelReply::finished(task_engine::TurnUsage::default(), 1),
    )
    .unwrap()
    .with_task_downloads(downloads)
}

#[test]
fn a_completed_handover_is_an_outcome_an_errands_prose_reply_may_report() {
    let mut fixture = errand();
    let id = task_engine::HandoverId::new("handover-1").unwrap();
    fixture.must_apply(Command::RequestHandover {
        handover_id: id.clone(),
    });
    // While the person still has the page, nothing has happened yet and the
    // task is parked rather than asked for another turn.
    assert!(!fixture.reducer.errand_outcome_witnessed());
    assert!(fixture
        .reducer
        .next_agent_command(None, &Digest)
        .unwrap()
        .is_none());
    fixture.must_apply(Command::CompleteHandover(
        task_engine::HandoverCompletion::new(
            id,
            task_engine::ActorLeaseId::new("lease-before"),
            task_engine::ActorLeaseId::new("lease-after"),
            task_engine::PersonInput::observed(2),
        )
        .unwrap(),
    ));
    // Decision 0136 section 5's third arm. The person was handed the page, did
    // the one thing only they could do — a CAPTCHA, a sign-in, an OTP — and
    // handed it back, so something was accomplished even though no browser
    // action of Taffy's carries it. Only the download arm was ever read here,
    // and the form arm is unreachable, so an errand that ends in a hand-back
    // could not finish at all: three prose replies and it died claiming it
    // could not read enough.
    assert!(fixture.reducer.errand_outcome_witnessed());
    prose_reply(&mut fixture);
    assert!(matches!(
        fixture.reducer.next_agent_command(None, &Digest).unwrap(),
        Some(Command::ResultCandidateReady)
    ));
}

#[test]
fn form_preparation_and_download_cancellation_cannot_witness_completion() {
    for intent in [
        BrowserIntent::FormToggle {
            tab: TabId::new("tab_1"),
            field: bip_types::identity::SemanticNodeId::new("checkbox-1"),
            checked: true,
        },
        BrowserIntent::DownloadCancel {
            tab: TabId::new("tab_1"),
            browser_session_id: task_engine::BrowserSessionId::new(BROWSER_SESSION).unwrap(),
            download_id: "download-1".to_owned(),
        },
    ] {
        let mut seed = common::errand::errand_seed();
        seed.snapshot
            .tool_allowlist
            .push(intent.tool_name().to_owned());
        let mut fixture = common::draft_from(seed);
        let mut preview = common::preview();
        preview.source_discovery_enabled = true;
        preview.new_source_cap = 4;
        preview.budgets = task_engine::TaskBudgets::none()
            .with(task_engine::BudgetKind::MaxSources, 5)
            .with(task_engine::BudgetKind::MaxModelRequests, 64);
        preview.provider_route = task_engine::ProviderRouteId::new("direct_user_key").ok();
        fixture.must_apply(Command::StartTask(preview));
        fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
        fixture.must_apply(Command::ExecutorStarted);
        let (action, dispatch) = dispatch_authorized(&mut fixture, intent, 0);
        let mut outcome = discovered_outcome(dispatch, source(7, "https://example.test"));
        outcome.discovered_source = None;
        fixture.must_apply(Command::RecordActionOutcome {
            action_id: ActionId::new(action),
            outcome: Box::new(outcome),
        });
        assert!(!fixture.reducer.errand_outcome_witnessed());
        prose_reply(&mut fixture);
        assert!(matches!(
            fixture.reducer.next_agent_command(None, &Digest).unwrap(),
            Some(Command::RequestModelTurn { .. })
        ));
    }
}

#[test]
fn an_errand_kind_completes_with_no_facts_where_research_would_be_partial() {
    let mut research = errand();
    prose_reply(&mut research);
    // Research: the same shape, no facts and no artifacts, ends partial.
    research.must_apply(Command::ResultCandidateReady);
    let next = research
        .reducer
        .next_agent_command(None, &Digest)
        .unwrap_or_else(|error| panic!("{error:?}"));
    assert!(
        matches!(next, Some(Command::PartialResultValidated(_))),
        "{next:?}"
    );

    let mut seed = common::errand::errand_seed();
    seed.kind = TaskKind::Errand;
    let mut errand = common::draft_from(seed);
    let mut preview = common::preview();
    preview.scope = task_engine::SourceScope::new();
    preview.sources.clear();
    preview.source_discovery_enabled = true;
    preview.new_source_cap = 4;
    preview.provider_route = task_engine::ProviderRouteId::new("direct_user_key").ok();
    preview.budgets = task_engine::TaskBudgets::none()
        .with(task_engine::BudgetKind::MaxSources, 4)
        .with(task_engine::BudgetKind::MaxModelRequests, 64);
    errand.must_apply(Command::StartTask(preview));
    errand.must_apply(Command::AcceptInitialConsent(common::receipt()));
    errand.must_apply(Command::RecordDiscoveryTab {
        discovery_tab_id: TabId::new(DISCOVERY_TAB),
        browser_session_id: task_engine::BrowserSessionId::new(BROWSER_SESSION)
            .unwrap_or_else(|_| unreachable!()),
    });
    errand.must_apply(Command::ExecutorStarted);
    // The errand has to have done the thing it exists for. Completing a
    // hand-back is one of the three outcomes that witness it (decision 0136
    // section 5); without one, an errand with no facts is partly done rather
    // than done, which is the distinction this half of the test is not about.
    let handover = task_engine::HandoverId::new("handover-complete").unwrap();
    errand.must_apply(Command::RequestHandover {
        handover_id: handover.clone(),
    });
    errand.must_apply(Command::CompleteHandover(
        task_engine::HandoverCompletion::new(
            handover,
            task_engine::ActorLeaseId::new("lease-before"),
            task_engine::ActorLeaseId::new("lease-after"),
            task_engine::PersonInput::observed(1),
        )
        .unwrap(),
    ));
    prose_reply(&mut errand);
    errand.must_apply(Command::ResultCandidateReady);
    let next = errand
        .reducer
        .next_agent_command(None, &Digest)
        .unwrap_or_else(|error| panic!("{error:?}"));
    let Some(Command::CompleteResultValidated(result)) = next else {
        panic!("an errand kind completes: {next:?}");
    };
    assert!(result.is_complete_for(TaskKind::Errand));
    assert!(!result.is_complete_for(TaskKind::Research));
    errand.must_apply(Command::CompleteResultValidated(result));
    assert_eq!(errand.state(), TaskState::Completed);
}
