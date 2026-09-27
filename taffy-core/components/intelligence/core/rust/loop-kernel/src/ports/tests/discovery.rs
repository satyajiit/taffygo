// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Observation input follows journalled source changes, never initial consent.

use bip_types::identity::{ContentDigest, DigestAlgorithm, DispatchId, MonotonicMillis, TabId};
use task_engine::{
    ActionOutcome, Authorization, BudgetKind, CapabilityId, ConsentedSource, ProposalDecision,
    ProviderRouteId, SourceId,
};

use super::*;

fn source(byte: u8, origin: &str) -> ConsentedSource {
    ConsentedSource {
        source_id: SourceId::from_bytes([byte; 16]),
        tab_id: TabId::new("discovery-tab"),
        normalized_origin: origin.to_owned(),
        canonical_locator: Some(format!("{origin}/start")),
    }
}

fn prepared(initial: Option<ConsentedSource>) -> (Reducer<ManualClock, SequentialIds>, TaskSeed) {
    let mut seed = seed();
    seed.snapshot.milestone = Milestone::M5;
    seed.snapshot.tool_allowlist = vec!["browser.navigate".into(), "browser.dom.read".into()];
    let mut reducer = Reducer::create(
        seed.clone(),
        BudgetDefaults::uniform(8),
        ManualClock::at(1_000),
        SequentialIds::new(),
        IdempotencyKey::new("create"),
        TraceId::new("create"),
    );
    let mut consent = initial_consent();
    consent.preview.new_source_cap = 2;
    consent.preview.budgets =
        TaskBudgets::none().with(BudgetKind::MaxSources, 2 + u64::from(initial.is_some()));
    consent.preview.provider_route = Some(ProviderRouteId::new("direct_user_key").expect("route"));
    if let Some(source) = initial {
        consent.preview.scope = SourceScope::new().include(source.source_id);
        consent.preview.sources.push(source);
    }
    super::super::apply_initial_consent(&mut reducer, consent).expect("consent");
    // Unconditionally, now that an errand asked from a page gets a blank tab
    // of its own beside that page (decision 0224).
    apply(
        &mut reducer,
        Command::RecordDiscoveryTab {
            discovery_tab_id: TabId::new("discovery-tab"),
            browser_session_id: BrowserSessionId::new("browser-session-1").expect("session"),
        },
        "tab",
    );
    apply(&mut reducer, Command::ExecutorStarted, "run");
    (reducer, seed)
}

/// The page the person was on when they asked, on a tab that is not the
/// blank one.
fn page_source(byte: u8) -> ConsentedSource {
    ConsentedSource {
        source_id: SourceId::from_bytes([byte; 16]),
        tab_id: TabId::new("page-tab"),
        normalized_origin: "https://asked-from.test".to_owned(),
        canonical_locator: Some("https://asked-from.test/start".to_owned()),
    }
}

fn discover(reducer: &mut Reducer<ManualClock, SequentialIds>, next: ConsentedSource) {
    let before = reducer.pre_model_observation_sources();
    let proposal = ActionProposal::new(
        ActionIntent::Browser(BrowserIntent::Navigate {
            tab: next.tab_id.clone(),
            address: next.canonical_locator.clone().expect("public locator"),
            new_tab: false,
        }),
        None,
        IdempotencyKey::new("navigate"),
        false,
        None,
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "0".repeat(64),
        },
    );
    apply(
        reducer,
        Command::ProposeAction(Box::new(proposal)),
        "propose",
    );
    let action = reducer
        .actions()
        .next()
        .expect("action")
        .action_id()
        .clone();
    let dispatch = DispatchId::new("dispatch");
    apply(
        reducer,
        Command::RecordPolicyDecision {
            action_id: action.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: CapabilityId::new("capability"),
            })),
            dispatch_id: Some(dispatch.clone()),
        },
        "authorize",
    );
    // A destination and grant alone never promote a page into model input.
    assert_eq!(reducer.pre_model_observation_sources(), before);
    apply(
        reducer,
        Command::RecordActionOutcome {
            action_id: action,
            outcome: Box::new(ActionOutcome {
                code: bip_types::ActionResultCode::Verified,
                dispatch_id: Some(dispatch),
                observed_at: MonotonicMillis(1_000),
                observation: None,
                discovered_source: Some(next),
            }),
        },
        "landed",
    );
}

/// An errand asked from a page holds that page and a blank tab at once, and
/// the two facts answer two different questions.
///
/// `empty_page_tab_id` is where a call with no page acts, so it must be the
/// page's tab — a reload or a read after a navigation belongs on the page the
/// task is working. `discovery_tab_id` is where a search acts, and it is the
/// blank tab. Before decision 0224 this shape could not exist at all:
/// discovery authority ended at the first bound source, so an errand asked
/// from a page never had a blank tab and sent its search through the person's
/// own (decision 0224).
#[test]
fn an_errand_asked_from_a_page_names_the_page_and_its_blank_tab_separately() {
    let page = page_source(9);
    let (mut reducer, _) = prepared(Some(page.clone()));
    assert_eq!(
        reducer
            .discovery_authority_facts()
            .map(|facts| facts.discovery_tab_id),
        Some("discovery-tab".to_owned())
    );
    let facts = reducer.model_turn_facts();
    assert_eq!(facts.discovery_tab_id.as_deref(), Some("discovery-tab"));
    assert_eq!(
        facts.empty_page_tab_id.as_deref(),
        Some(page.tab_id.as_str()),
        "a call with no page belongs on the page the task is working"
    );

    // The authority is good for one landing: the browser froze the cap when
    // it opened the tab, so a fact sent after anything is spent is answered
    // `kTabGone`. The tab is not the authority and does not end with it — a
    // second search belongs in it exactly as the first did, and taking this
    // from the authority sent that search back through the person's own tab,
    // which is the whole defect one step later.
    discover(&mut reducer, source(2, "https://official.test"));
    assert!(reducer.discovery_authority_facts().is_none());
    assert_eq!(
        reducer.model_turn_facts().discovery_tab_id.as_deref(),
        Some("discovery-tab"),
        "the tab outlives the authority that opened it"
    );
}

#[test]
fn observation_sources_follow_verified_discovery_and_its_durable_replay() {
    let (mut reducer, seed) = prepared(None);
    let admitted = source(1, "https://official.test");
    assert!(reducer.pre_model_observation_sources().is_empty());
    discover(&mut reducer, admitted.clone());
    assert!(reducer.task().snapshot().consented_sources.is_empty());
    assert_eq!(
        reducer.pre_model_observation_sources(),
        vec![admitted.clone()]
    );
    assert_eq!(reducer.task().snapshot().remaining_new_source_cap, 1);
    let (restored, _) = Reducer::replay(
        seed,
        BudgetDefaults::uniform(8),
        ManualClock::at(1_000),
        SequentialIds::new(),
        reducer.journal(),
    )
    .expect("replay committed discovery");
    assert_eq!(restored.pre_model_observation_sources(), vec![admitted]);
    assert!(restored.task().snapshot().consented_sources.is_empty());
}

#[test]
fn observation_sources_replace_the_initial_site_and_withdraw_excluded_pages() {
    let initial = source(1, "https://search.test");
    let (mut reducer, _) = prepared(Some(initial.clone()));
    let landed = source(2, "https://official.test");
    discover(&mut reducer, landed.clone());
    assert_eq!(reducer.task().snapshot().consented_sources, vec![initial]);
    assert_eq!(
        reducer.pre_model_observation_sources(),
        vec![landed.clone()]
    );
    apply(
        &mut reducer,
        Command::ExcludeSource {
            source_id: landed.source_id,
        },
        "exclude",
    );
    assert!(reducer.pre_model_observation_sources().is_empty());
}

fn selected_replay() -> (TaskSeed, InitialConsentAdmission) {
    let mut seed = seed();
    seed.kind = TaskKind::Errand;
    seed.snapshot.milestone = Milestone::M7;
    seed.snapshot.skill_version_id = Some(bip_types::identity::SkillVersionId(
        "recorded-download@1".to_owned(),
    ));
    seed.snapshot.tool_allowlist = vec!["browser.dom.read".into(), "user.handover".into()];
    let source = source(1, "https://official.test");
    let mut consent = initial_consent();
    consent.preview.scope = SourceScope::new().include(source.source_id);
    consent.preview.sources = vec![source];
    consent.preview.source_discovery_enabled = false;
    consent.preview.new_source_cap = 0;
    consent.preview.provider_route = Some(
        ProviderRouteId::new(task_engine::REVIEWED_NO_MODEL_ROUTE_ID).expect("reviewed route"),
    );
    consent.preview.budgets = TaskBudgets::none()
        .with(BudgetKind::MaxSources, 1)
        .with(BudgetKind::MaxModelRequests, 0);
    (seed, consent)
}

#[test]
fn accepted_model_free_recording_keeps_its_exact_closed_consent_after_replay() {
    let (seed, consent) = selected_replay();
    let expected = consent.preview.clone();
    let mut reducer = Reducer::create(
        seed.clone(),
        BudgetDefaults::uniform(8),
        ManualClock::at(1_000),
        SequentialIds::new(),
        IdempotencyKey::new("create"),
        TraceId::new("create"),
    );
    super::super::apply_initial_consent(&mut reducer, consent).expect("reviewed acceptance");
    let facts = reducer.view_facts().expect("accepted local consent");
    let accepted = facts.accepted_consent.expect("standing consent");
    assert_eq!(accepted.sources, expected.sources);
    assert!(!accepted.source_discovery_enabled);
    assert_eq!(accepted.new_source_cap, 0);
    assert_eq!(
        accepted.provider_route_id.as_deref(),
        Some(task_engine::REVIEWED_NO_MODEL_ROUTE_ID)
    );
    let (restored, _) = Reducer::replay(
        seed,
        BudgetDefaults::uniform(8),
        ManualClock::at(1_000),
        SequentialIds::new(),
        reducer.journal(),
    )
    .expect("durable recorded-flow start");
    assert_eq!(
        restored
            .view_facts()
            .expect("restored consent")
            .accepted_consent,
        Some(accepted)
    );
}

#[test]
fn local_replay_projection_does_not_admit_adjacent_unreviewed_start_shapes() {
    for mutation in 0..10 {
        let (mut seed, mut consent) = selected_replay();
        match mutation {
            0 => seed.snapshot.skill_version_id = None,
            1 => {
                seed.snapshot.skill_version_id =
                    Some(bip_types::identity::SkillVersionId(String::new()));
            }
            2 => seed.kind = TaskKind::Research,
            3 => seed.control_mode = ControlMode::Shared,
            4 => {
                consent.preview.sources.clear();
                consent.preview.scope = SourceScope::new();
                consent.preview.budgets = TaskBudgets::none()
                    .with(BudgetKind::MaxSources, 0)
                    .with(BudgetKind::MaxModelRequests, 0);
            }
            5 => consent.preview.source_discovery_enabled = true,
            6 => consent.preview.new_source_cap = 1,
            7 => {
                consent.preview.budgets = consent
                    .preview
                    .budgets
                    .clone()
                    .with(BudgetKind::MaxModelRequests, 1);
            }
            8 => {
                consent.preview.budgets = TaskBudgets::none().with(BudgetKind::MaxSources, 1);
            }
            9 => {
                consent.preview.provider_route =
                    Some(ProviderRouteId::new("direct_user_key").expect("ordinary errand"));
            }
            _ => unreachable!(),
        }
        let mut reducer = Reducer::create(
            seed,
            BudgetDefaults::uniform(8),
            ManualClock::at(1_000),
            SequentialIds::new(),
            IdempotencyKey::new("create"),
            TraceId::new("create"),
        );
        assert!(
            super::super::apply_initial_consent(&mut reducer, consent).is_err(),
            "mutation {mutation} must not become standing consent"
        );
        assert!(reducer
            .view_facts()
            .expect("unaccepted task")
            .accepted_consent
            .is_none());
    }
}

/// A turn composed from no page still names the tab the task is on.
///
/// A running errand whose tab has just left its source has no page
/// projection. Naming nothing put the empty string into every call that
/// designates no node, which the canonical intent encoding refuses — and that
/// refusal ends the walk rather than the call, leaving the task with no next
/// command at all (decision 0178).
///
/// A task that holds both a source tab and a blank tab names the source tab
/// here, and the order is the whole of the rule: this is where a call with no
/// page acts, so a reload or a read after a navigation must land back on the
/// page the task is working, never on a blank tab it has no business reading.
/// Where a *search* acts is a different question and a different field
/// (decision 0224).
#[test]
fn a_turn_with_no_page_names_the_tab_the_task_is_on() {
    let (mut reducer, _) = prepared(None);
    assert_eq!(
        reducer.model_turn_facts().empty_page_tab_id.as_deref(),
        Some("discovery-tab"),
        "a zero-source turn names the discovery tab"
    );
    let landed = source(1, "https://official.test");
    discover(&mut reducer, landed.clone());
    assert!(
        reducer.discovery_authority_facts().is_none(),
        "the blank tab is good for one landing and this was it"
    );
    assert_eq!(
        reducer.model_turn_facts().empty_page_tab_id.as_deref(),
        Some(landed.tab_id.as_str()),
        "and the turn then names the source's own tab"
    );
    apply(
        &mut reducer,
        Command::ExcludeSource {
            source_id: landed.source_id,
        },
        "exclude",
    );
    // Withdrawing the only discovered page returns the task to discovery, so
    // the discovery tab answers again. What must never happen is the third
    // answer: a value that is present and empty.
    assert_eq!(
        reducer.model_turn_facts().empty_page_tab_id.as_deref(),
        Some("discovery-tab")
    );
    assert!(reducer
        .model_turn_facts()
        .empty_page_tab_id
        .is_none_or(|tab| !tab.is_empty()));
}
