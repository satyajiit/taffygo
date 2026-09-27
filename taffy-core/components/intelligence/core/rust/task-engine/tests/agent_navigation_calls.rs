// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed navigation operands and their proposal binding.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::TabId;
use task_engine::agent::ModelToolCall;
use task_engine::{
    ArgumentValue, CallVerdict, Command, ModelStopReason, NotAttempted, SuppliedArgument,
};

use common::agent::{navigate_call, with_recorded_turn, Digest};

#[test]
fn an_in_tab_navigate_is_proposed_with_the_address_and_no_node() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![navigate_call("https://example.test/next")],
    );
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected a navigation to be proposed, got {next:?}");
    };
    assert_eq!(proposal.tool_name(), "browser.navigate");
    assert_eq!(proposal.action_class(), task_engine::ActionClass::OpenLink);
    assert_eq!(proposal.node_id(), None);
    assert_eq!(proposal.tab_id(), &TabId::new("tab_1"));
    assert_eq!(
        proposal.destination_address(),
        Some("https://example.test/next")
    );
    assert_eq!(
        proposal.idempotency(),
        task_engine::IdempotencyClass::ConditionallyIdempotent
    );
}

#[test]
fn a_new_tab_argument_is_outside_the_navigation_contract_and_never_proposed() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![ModelToolCall::new(
            "browser.navigate",
            vec![
                SuppliedArgument::new(
                    "address",
                    ArgumentValue::Address("https://example.test/next".to_owned()),
                ),
                SuppliedArgument::new("new_tab", ArgumentValue::Flag(true)),
            ],
        )],
    );
    assert_eq!(
        fixture
            .reducer
            .turn_dispositions(&residency)
            .first()
            .map(|disposition| disposition.verdict),
        Some(CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected))
    );
    assert!(!matches!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap(),
        Some(Command::ProposeAction(_))
    ));
}

#[test]
fn two_addresses_on_the_same_tab_do_not_share_a_proposal_digest() {
    let (fixture_a, residency_a) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![navigate_call("https://example.test/a")],
    );
    let (fixture_b, residency_b) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![navigate_call("https://example.test/b")],
    );
    let next_a = fixture_a
        .reducer
        .next_agent_command(Some(&residency_a), &Digest)
        .unwrap();
    let next_b = fixture_b
        .reducer
        .next_agent_command(Some(&residency_b), &Digest)
        .unwrap();
    let Some(Command::ProposeAction(proposal_a)) = next_a else {
        panic!("expected a navigation to be proposed, got {next_a:?}");
    };
    let Some(Command::ProposeAction(proposal_b)) = next_b else {
        panic!("expected a navigation to be proposed, got {next_b:?}");
    };
    assert_ne!(proposal_a.proposal_digest, proposal_b.proposal_digest);
}
