// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! `user.request_values` may name only a line a person's values can go into.
//!
//! The browser draws a sheet only for a form or a field that takes text, and
//! gives up on anything else without drawing one. On a phone an errand named
//! an FAQ heading as the form four times in ten seconds and learned nothing
//! from any of them; the fifth call named the form's region and drew the sheet
//! (verification report, section 2.48). Refused on sight, the call is answered
//! with what to name instead.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use task_engine::agent::{ModelToolCall, TurnPage};
use task_engine::{ArgumentValue, CallVerdict, ModelStopReason, NotAttempted, SuppliedArgument};

use common::agent::{field_page, form_page, page, with_recorded_turn_on};

fn verdict_of_asking_about(page: TurnPage, node: u32) -> Option<CallVerdict> {
    let call = ModelToolCall::new(
        "user.request_values",
        vec![SuppliedArgument::new("form", ArgumentValue::Handle(node))],
    );
    let (fixture, residency) = with_recorded_turn_on(page, ModelStopReason::ToolCall, vec![call]);
    fixture
        .reducer
        .turn_dispositions(&residency)
        .first()
        .map(|disposition| disposition.verdict)
}

#[test]
fn a_line_that_takes_no_values_is_refused_on_sight() {
    let (page, handle) = page();
    assert_eq!(
        verdict_of_asking_about(page, handle.value()),
        Some(CallVerdict::NotAttempted(NotAttempted::NotAField))
    );
}

#[test]
fn a_form_or_a_field_is_asked_about() {
    for (page, handle) in [form_page(), field_page()] {
        assert!(!matches!(
            verdict_of_asking_about(page, handle.value()),
            Some(CallVerdict::NotAttempted(_)) | None
        ));
    }
}

#[test]
fn an_unknown_number_is_still_unknown_rather_than_not_a_field() {
    let (page, handle) = form_page();
    assert_eq!(
        verdict_of_asking_about(page, handle.value() + 1),
        Some(CallVerdict::NotAttempted(NotAttempted::HandleUnknown))
    );
}
