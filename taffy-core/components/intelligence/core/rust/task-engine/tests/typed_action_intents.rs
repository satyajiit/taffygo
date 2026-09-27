// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! End-to-end projection of validated M3 calls into typed, content-free intents.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::TabId;
use task_engine::action::{
    ActionIntent, ActionProposal, BrowserIntent, DisclosureState, OpaqueOperandKind,
    OpaqueOperandRef,
};
use task_engine::agent::{ModelToolCall, TurnResidency};
use task_engine::{
    ArgumentValue, CallVerdict, Command, ModelStopReason, NotAttempted, SuppliedArgument,
};

use common::agent::{form_page, with_recorded_turn, with_recorded_turn_on, Digest};

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
fn navigation_and_history_preserve_every_operand() {
    let (navigate, _) = proposed(ModelToolCall::new(
        "browser.navigate",
        vec![argument(
            "address",
            ArgumentValue::Address("https://example.test/a".to_owned()),
        )],
    ));
    assert!(matches!(
        browser(&navigate),
        BrowserIntent::Navigate { address, new_tab: false, .. }
            if address == "https://example.test/a"
    ));

    assert!(matches!(
        browser(&proposed(ModelToolCall::new("browser.back", vec![])).0),
        BrowserIntent::HistoryBack { .. }
    ));
    assert!(matches!(
        browser(&proposed(ModelToolCall::new("browser.forward", vec![])).0),
        BrowserIntent::HistoryForward { .. }
    ));
    assert!(matches!(
        browser(&proposed(ModelToolCall::new("browser.reload", vec![])).0),
        BrowserIntent::Reload { .. }
    ));
    assert!(matches!(
        browser(&proposed(ModelToolCall::new("browser.stop_loading", vec![])).0),
        BrowserIntent::StopLoading { .. }
    ));
}

#[test]
fn only_the_dedicated_task_tab_operation_is_proposable() {
    let navigate = ActionIntent::Browser(BrowserIntent::Navigate {
        tab: TabId::new("tab_1"),
        address: "https://example.test/new".to_owned(),
        new_tab: true,
    });
    assert!(!navigate.is_proposable());

    let tabs_open = ActionIntent::Browser(BrowserIntent::TabsOpen {
        context: TabId::new("tab_1"),
        address: Some("https://example.test/tab".to_owned()),
    });
    let encoded = tabs_open.encode_canonical().expect("typed intent");
    assert_eq!(
        ActionIntent::decode_canonical(&encoded),
        Ok(tabs_open.clone())
    );
    assert!(tabs_open.is_proposable());

    let navigate_call = ModelToolCall::new(
        "browser.navigate",
        vec![
            argument(
                "address",
                ArgumentValue::Address("https://example.test/new".to_owned()),
            ),
            argument("new_tab", ArgumentValue::Flag(true)),
        ],
    );
    let (fixture, residency) = with_recorded_turn(ModelStopReason::ToolCall, vec![navigate_call]);
    assert_eq!(
        only_verdict(&fixture, &residency),
        Some(CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected))
    );

    let open_call = ModelToolCall::new(
        "browser.tabs.open",
        vec![argument(
            "address",
            ArgumentValue::Address("https://example.test/tab".to_owned()),
        )],
    );
    let (fixture, residency) = with_recorded_turn(ModelStopReason::ToolCall, vec![open_call]);
    assert_eq!(
        only_verdict(&fixture, &residency),
        Some(CallVerdict::Attemptable)
    );
}

#[test]
fn disclosure_activation_binds_and_round_trips_its_exact_expected_state() {
    let call = ModelToolCall::new(
        "browser.dom.click",
        vec![
            argument("node", ArgumentValue::Handle(0)),
            argument(
                "expected_state",
                ArgumentValue::Choice("expanded".to_owned()),
            ),
        ],
    );
    let (proposal, _) = proposed(call);
    assert!(matches!(
        browser(&proposal),
        BrowserIntent::DomClick {
            expected_state: Some(DisclosureState::Expanded),
            ..
        }
    ));
    assert!(proposal.intent().is_proposable());
    let encoded = proposal.intent().encode_canonical().expect("typed intent");
    assert_eq!(
        ActionIntent::decode_canonical(&encoded),
        Ok(proposal.intent().clone())
    );
    let mut invalid_state = encoded;
    let state = invalid_state.last_mut().expect("encoded state byte");
    *state = 3;
    assert!(ActionIntent::decode_canonical(&invalid_state).is_err());
}

#[test]
fn an_ordinary_press_names_no_state_and_keeps_the_third_wire_tag() {
    let call = ModelToolCall::new(
        "browser.dom.click",
        vec![argument("node", ArgumentValue::Handle(0))],
    );
    let (proposal, _) = proposed(call);
    assert!(matches!(
        browser(&proposal),
        BrowserIntent::DomClick {
            expected_state: None,
            ..
        }
    ));
    assert!(proposal.intent().is_proposable());
    let encoded = proposal.intent().encode_canonical().expect("typed intent");
    assert_eq!(*encoded.last().expect("encoded state byte"), 2u8);
    assert_eq!(
        ActionIntent::decode_canonical(&encoded),
        Ok(proposal.intent().clone())
    );

    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "browser.dom.click",
            vec![argument("node", ArgumentValue::Handle(0))],
        )],
    );
    assert_eq!(
        only_verdict(&fixture, &residency),
        Some(CallVerdict::Attemptable)
    );
}

#[test]
fn link_open_carries_the_exact_numbered_observation_without_a_destination() {
    let call = ModelToolCall::new(
        "browser.link.open",
        vec![argument("node", ArgumentValue::Handle(0))],
    );
    let (proposal, _) = proposed(call);
    assert!(matches!(
        browser(&proposal),
        BrowserIntent::LinkOpen { target }
            if target.tab_id().as_str() == "tab_1"
                && target.frame_id().as_str() == "frame_1"
                && target.page_epoch().as_str() == "epoch_1"
                && target.graph_revision().0 == 4
                && target.node_id().as_str() == "n-1"
                && target.expected_origin_value() == "https://example.test"
    ));
    assert_eq!(proposal.destination_address(), None);
    assert_eq!(proposal.action_class(), task_engine::ActionClass::OpenLink);
    assert!(proposal.intent().is_proposable());

    let encoded = proposal
        .intent()
        .encode_canonical()
        .expect("link intent is bounded");
    assert_eq!(
        ActionIntent::decode_canonical(&encoded),
        Ok(proposal.intent().clone())
    );
    // The expected source origin is part of freshness identity. No resolved
    // link destination crosses this seam; only the browser registry owns it.
    assert!(encoded
        .windows("https://example.test".len())
        .any(|window| window == b"https://example.test"));
}

#[test]
fn scroll_requires_the_exact_node_targeted_variant() {
    for call in [
        ModelToolCall::new(
            "browser.dom.scroll",
            vec![argument(
                "direction",
                ArgumentValue::Choice("down".to_owned()),
            )],
        ),
        ModelToolCall::new(
            "browser.dom.scroll",
            vec![argument(
                "direction",
                ArgumentValue::Choice("to_node".to_owned()),
            )],
        ),
    ] {
        let (fixture, residency) = with_recorded_turn(ModelStopReason::ToolCall, vec![call]);
        assert!(matches!(
            only_verdict(&fixture, &residency),
            Some(CallVerdict::NotAttempted(_))
        ));
    }
}

#[test]
fn task_tab_listing_is_attemptable_and_unknown_handles_fail_closed() {
    for (name, arguments, verdict) in [
        ("browser.tabs.list", vec![], CallVerdict::Attemptable),
        (
            "browser.tabs.activate",
            vec![argument("tab", ArgumentValue::Handle(0))],
            CallVerdict::NotAttempted(NotAttempted::HandleUnknown),
        ),
        (
            "browser.tabs.close",
            vec![argument("tab", ArgumentValue::Handle(0))],
            CallVerdict::NotAttempted(NotAttempted::HandleUnknown),
        ),
    ] {
        let (fixture, residency) = with_recorded_turn(
            ModelStopReason::ToolCall,
            vec![ModelToolCall::new(name, arguments)],
        );
        assert_eq!(only_verdict(&fixture, &residency), Some(verdict), "{name}");
    }
}

#[test]
fn request_values_resolves_the_form_and_emits_the_correlated_command() {
    let (fixture, residency) = with_recorded_turn_on(
        form_page().0,
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "user.request_values",
            vec![argument("form", ArgumentValue::Handle(0))],
        )],
    );
    let command = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .expect("decision");
    let Some(Command::RequestFieldValues {
        request_id,
        tab_id,
        node_id,
        companion_node_ids,
    }) = command
    else {
        panic!("expected field-value request, got {command:?}");
    };
    assert_eq!(request_id.as_str(), "turn-0-values-0");
    assert_eq!(tab_id.as_str(), "tab_1");
    assert_eq!(node_id.as_str(), "n-1");
    // A form brings no companions: the browser expands a form itself.
    assert!(companion_node_ids.is_empty());
}

#[test]
fn a_search_query_is_digest_bound_and_proposable_only_with_live_residency() {
    let make = |digest| {
        ActionIntent::Browser(BrowserIntent::Search {
            tab: TabId::new("tab_1"),
            query: OpaqueOperandRef::for_call(0, 0, OpaqueOperandKind::SearchQuery, digest),
        })
    };
    let first = make([1; 32]);
    let second = make([2; 32]);
    assert_ne!(first.encode_canonical(), second.encode_canonical());
    assert!(first.is_proposable());
    assert!(second.is_proposable());

    let call = ModelToolCall::new(
        "browser.search",
        vec![argument("query", ArgumentValue::Text("alpha".to_owned()))],
    );
    let (fixture, residency) = with_recorded_turn(ModelStopReason::ToolCall, vec![call]);
    assert_eq!(
        only_verdict(&fixture, &residency),
        Some(CallVerdict::Attemptable)
    );
}

#[test]
fn live_calls_and_canonical_restore_share_address_bounds() {
    let call = ModelToolCall::new(
        "browser.navigate",
        vec![argument("address", ArgumentValue::Address("x".to_owned()))],
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
