// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed projection and authority bounds for page-reading operations.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::TabId;
use task_engine::action::{
    ActionIntent, ActionProposal, BrowserIntent, DomQueryRole, OpaqueOperandKind, ScrollDirection,
};
use task_engine::agent::{ModelToolCall, TurnResidency};
use task_engine::{
    ArgumentValue, CallVerdict, Command, ModelStopReason, NotAttempted, SuppliedArgument,
};

use common::agent::{with_recorded_turn, Digest};

fn argument(name: &str, value: ArgumentValue) -> SuppliedArgument {
    SuppliedArgument::new(name, value)
}

fn proposed(call: ModelToolCall) -> (Box<ActionProposal>, TurnResidency) {
    let (fixture, residency) = with_recorded_turn(ModelStopReason::ToolCall, vec![call]);
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .expect("decision");
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected proposal, got {next:?}");
    };
    (proposal, residency)
}

fn browser(proposal: &ActionProposal) -> &BrowserIntent {
    let ActionIntent::Browser(intent) = proposal.intent() else {
        panic!("expected browser intent");
    };
    intent
}

#[test]
fn whole_document_read_and_served_node_calls_preserve_their_exact_scope() {
    let (read, _) = proposed(ModelToolCall::new("browser.dom.read", vec![]));
    assert!(matches!(
        browser(&read),
        BrowserIntent::DomRead { target: None, .. }
    ));
    assert_eq!(read.node_id(), None);

    let (form, _) = proposed(ModelToolCall::new(
        "browser.form.inspect",
        vec![argument("form", ArgumentValue::Handle(0))],
    ));
    assert_eq!(
        form.node_id()
            .map(bip_types::identity::SemanticNodeId::as_str),
        Some("n-1")
    );

    let (scroll, _) = proposed(ModelToolCall::new(
        "browser.dom.scroll",
        vec![
            argument("direction", ArgumentValue::Choice("to_node".to_owned())),
            argument("node", ArgumentValue::Handle(0)),
        ],
    ));
    assert!(matches!(
        browser(&scroll),
        BrowserIntent::DomScroll {
            direction: ScrollDirection::ToNode,
            target: Some(_),
            ..
        }
    ));

    let (selection, _) = proposed(ModelToolCall::new("browser.selection.read", vec![]));
    assert!(matches!(
        browser(&selection),
        BrowserIntent::SelectionRead { .. }
    ));
}

#[test]
fn screenshot_fallback_is_a_typed_task_only_document_read() {
    let intent = ActionIntent::Browser(BrowserIntent::PageScreenshotInspect {
        tab: TabId::new("tab_1"),
    });
    assert!(matches!(
        &intent,
        ActionIntent::Browser(BrowserIntent::PageScreenshotInspect { .. })
    ));
    assert_eq!(intent.node_id(), None);
    assert_eq!(intent.action_class(), task_engine::ActionClass::ObservePage);
    assert_eq!(intent.tool_name(), "page.screenshot.inspect");
    let encoded = intent.encode_canonical().expect("typed intent");
    assert_eq!(ActionIntent::decode_canonical(&encoded), Ok(intent));
}

#[test]
fn filtered_query_keeps_its_filters_but_projects_document_authority() {
    let (query, _) = proposed(ModelToolCall::new(
        "browser.dom.query",
        vec![
            argument("within", ArgumentValue::Handle(0)),
            argument("role", ArgumentValue::Choice("button".to_owned())),
            argument("text", ArgumentValue::Text("Pay now".to_owned())),
            argument("limit", ArgumentValue::Count(3)),
        ],
    ));
    let BrowserIntent::DomQuery {
        within,
        role,
        text,
        limit,
        ..
    } = browser(&query)
    else {
        panic!("expected DOM query");
    };
    assert_eq!(
        within
            .as_ref()
            .map(bip_types::identity::SemanticNodeId::as_str),
        Some("n-1")
    );
    assert_eq!(*role, Some(DomQueryRole::Button));
    assert_eq!(*limit, Some(3));
    let text = text.as_ref().expect("text is held by opaque reference");
    assert_eq!(text.kind(), OpaqueOperandKind::DomQueryText);
    assert!(text.handle().ends_with("-dom-query-text"));
    assert_eq!(query.node_id(), None);
    assert!(query.intent().is_proposable());

    let encoded = query.intent().encode_canonical().expect("typed intent");
    assert_eq!(
        ActionIntent::decode_canonical(&encoded),
        Ok(query.intent().clone())
    );
    assert!(encoded.windows("n-1".len()).any(|window| window == b"n-1"));
}

#[test]
fn targeted_dom_read_remains_typed_but_is_not_proposable() {
    let targeted = ActionIntent::Browser(BrowserIntent::DomRead {
        tab: TabId::new("tab_1"),
        target: Some(bip_types::identity::SemanticNodeId::new("n-1")),
    });
    let encoded = targeted.encode_canonical().expect("typed intent");
    assert_eq!(
        ActionIntent::decode_canonical(&encoded),
        Ok(targeted.clone())
    );
    assert!(!targeted.is_proposable());

    let call = ModelToolCall::new(
        "browser.dom.read",
        vec![argument("node", ArgumentValue::Handle(0))],
    );
    let (fixture, residency) = with_recorded_turn(ModelStopReason::ToolCall, vec![call]);
    assert_eq!(
        only_verdict(&fixture, &residency),
        Some(CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected))
    );
}

fn only_verdict(fixture: &common::Fixture, residency: &TurnResidency) -> Option<CallVerdict> {
    fixture
        .reducer
        .turn_dispositions(residency)
        .first()
        .map(|disposition| disposition.verdict)
}
