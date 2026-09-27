// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use task_engine::action::{OpaqueOperandKind, OpaqueOperandRef};

fn search() -> ActionEffectFacts {
    facts(
        "search",
        BrowserIntent::Search {
            tab: TabId::new("tab"),
            query: OpaqueOperandRef::for_call(1, 1, OpaqueOperandKind::SearchQuery, [3; 32]),
        },
    )
}

fn prelude() -> FlowRecording {
    let mut recording = FlowRecording::default();
    recording.begin();
    recording.observe_origin("https://search.test");
    let search = search();
    recording.dispatched(&search, &LivePage::new());
    recording.settled(&search, None, None);
    let read = facts(
        "search-read",
        BrowserIntent::DomRead {
            tab: TabId::new("tab"),
            target: None,
        },
    );
    recording.dispatched(&read, &page_at("https://search.test").0);
    recording.settled(&read, None, None);
    recording
}

fn destination_link() -> (LivePage, ActionEffectFacts) {
    let (page, target) = page_at("https://search.test");
    (
        page,
        facts("destination", BrowserIntent::LinkOpen { target }),
    )
}

#[test]
fn verified_discovered_destination_starts_the_review_after_search() {
    let mut recording = prelude();
    let (page, link) = destination_link();
    recording.dispatched(&link, &page);
    recording.settled(&link, Some(&source()), Some(&source()));
    recording.handover("person", false);
    recording.handover("person", true);
    let procedure = finished(&recording).unwrap();
    assert_eq!(procedure.steps.len(), 2);
    assert_eq!(procedure.steps[0].verb, "browser.navigate");
    assert_eq!(procedure.steps[1].verb, "user.handover");
    assert!(matches!(
        &procedure.steps[0].arguments[0].value,
        procedure_engine::StepValue::Literal(task_engine::tool::ArgumentValue::Address(address))
            if address == "https://example.test/entry"
    ));
    assert_eq!(procedure.scope.origin().display(), "https://example.test");
}

#[test]
fn previous_locator_wrong_tab_and_nonpublic_destination_cannot_start() {
    for (accepted, discovered) in [
        (source(), None),
        (
            ConsentedSource {
                tab_id: TabId::new("other-tab"),
                ..source()
            },
            Some(source()),
        ),
        (
            source(),
            Some(ConsentedSource {
                source_id: SourceId::from_bytes([8; 16]),
                ..source()
            }),
        ),
        (
            ConsentedSource {
                canonical_locator: Some("https://example.test/entry?token=secret".into()),
                ..source()
            },
            Some(ConsentedSource {
                canonical_locator: Some("https://example.test/entry?token=secret".into()),
                ..source()
            }),
        ),
    ] {
        let mut recording = prelude();
        let (page, link) = destination_link();
        recording.dispatched(&link, &page);
        recording.settled(&link, Some(&accepted), discovered.as_ref());
        assert!(finished(&recording).is_none());
        assert!(recording.steps.is_empty());
    }
}

#[test]
fn direct_navigation_records_executed_address_instead_of_stale_locator() {
    let mut recording = FlowRecording::default();
    recording.begin();
    let navigate = facts(
        "direct",
        BrowserIntent::Navigate {
            tab: TabId::new("tab"),
            address: "https://example.test/new-entry".into(),
            new_tab: false,
        },
    );
    recording.dispatched(&navigate, &LivePage::new());
    recording.settled(&navigate, Some(&source()), None);
    let procedure = finished(&recording).unwrap();
    assert!(matches!(
        &procedure.steps[0].arguments[0].value,
        procedure_engine::StepValue::Literal(task_engine::tool::ArgumentValue::Address(address))
            if address == "https://example.test/new-entry"
    ));
}

#[test]
fn personal_input_or_mutation_before_start_refuses_the_complete_record() {
    let mut person = prelude();
    person.handover("person", false);
    assert!(person.refused);
    let mut mutation = prelude();
    let (page, target) = page_at("https://search.test");
    mutation.dispatched(&facts("focus", BrowserIntent::DomFocus { target }), &page);
    assert!(mutation.refused);
}

#[test]
fn a_started_record_never_resets_to_hide_an_unsupported_step() {
    let mut recording = recorded_flow();
    let search = search();
    recording.dispatched(&search, &page().0);
    recording.settled(&search, None, None);
    let (page, link) = destination_link();
    recording.dispatched(&link, &page);
    recording.settled(&link, Some(&source()), Some(&source()));
    assert!(finished(&recording).is_none());
    assert!(recording.refused);
}

#[test]
fn unverified_or_overlapping_discovery_cannot_start_a_saved_flow() {
    let mut failed = prelude();
    let (page, mut link) = destination_link();
    failed.dispatched(&link, &page);
    link.state = ActionState::Failed;
    failed.settled(&link, Some(&source()), Some(&source()));
    assert!(failed.refused);
    let mut outstanding = FlowRecording::default();
    outstanding.begin();
    outstanding.dispatched(&search(), &LivePage::new());
    link.state = ActionState::Verified;
    outstanding.dispatched(&link, &page);
    outstanding.settled(&link, Some(&source()), Some(&source()));
    assert!(outstanding.refused);
}
