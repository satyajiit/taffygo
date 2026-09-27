// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! `browser.form.fill` may name only a field that can take text.
//!
//! The browser holds a person's values for the exact fields the sheet showed,
//! and a fill aimed at any other line ends that approval for every one of them.
//! A page prints a field twice — once with its label and no action, once as the
//! field that sets text — and the labelled one is the easy pick. Refused on
//! sight, the wrong pick costs one turn and not the person's answer
//! (decision 0195).

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::{SemanticNodeId, TabId};
use task_engine::agent::{ModelToolCall, TurnPage, TurnResidency};
use task_engine::{
    field_value_request_id_for_call, ArgumentValue, CallVerdict, Command, FieldValueAskOutcome,
    Milestone, ModelStopReason, NotAttempted, SuppliedArgument, SuppliedValueCount,
};

use common::agent::{field_page, form_page, reply};

/// A running task at the milestone that has fills, holding one supplied value.
fn holding_one_value() -> common::Fixture {
    let mut seed = common::seed();
    seed.snapshot.milestone = Milestone::M5;
    let mut fixture = common::draft_from(seed);
    fixture.must_apply(Command::StartTask(common::preview()));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    let request_id = field_value_request_id_for_call(0, 0);
    fixture.must_apply(Command::RequestFieldValues {
        request_id: request_id.clone(),
        tab_id: TabId::new("tab_1"),
        node_id: SemanticNodeId::new("n-1"),
        companion_node_ids: task_engine::FieldNodeIds::none(),
    });
    fixture.must_apply(Command::SupplyFieldValues {
        request_id,
        supplied: SuppliedValueCount::new(1).expect("one value"),
        outcome: Some(FieldValueAskOutcome::Answered),
        field_node_ids: None,
    });
    fixture
}

fn verdict_of_filling(page: TurnPage, node: u32) -> Option<CallVerdict> {
    let mut fixture = holding_one_value();
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let call = ModelToolCall::new(
        "browser.form.fill",
        vec![
            SuppliedArgument::new("field", ArgumentValue::Handle(node)),
            SuppliedArgument::new("value_from", ArgumentValue::SuppliedValue(0)),
        ],
    );
    let residency =
        TurnResidency::read(call_id, page, reply(ModelStopReason::ToolCall, vec![call]))
            .expect("a readable reply");
    fixture
        .reducer
        .turn_dispositions(&residency)
        .first()
        .map(|disposition| disposition.verdict)
}

#[test]
fn a_fill_that_names_the_form_is_refused_on_sight() {
    let (page, handle) = form_page();
    assert_eq!(
        verdict_of_filling(page, handle.value()),
        Some(CallVerdict::NotAttempted(NotAttempted::NotATextField))
    );
}

#[test]
fn a_fill_that_names_a_field_that_takes_text_is_not_refused_on_sight() {
    let (page, handle) = field_page();
    assert!(!matches!(
        verdict_of_filling(page, handle.value()),
        Some(CallVerdict::NotAttempted(_)) | None
    ));
}
