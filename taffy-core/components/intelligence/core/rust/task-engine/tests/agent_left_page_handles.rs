// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A number read from a page its tab has since left is refused on sight.
//!
//! Numbers are task-global, so one read from an earlier document still
//! resolves. Before this, such a number reached the browser, came back
//! `at=page-epoch` as "the page changed since it was read; read it again", and
//! the model read again and named it again (verification report, section
//! 2.48).
//!
//! It answers its own reason rather than sharing `HandleUnknown`'s. That word
//! means "nobody printed that number for you", whose advice is to use one from
//! the latest snapshot — which is the wrong instruction for a number that
//! resolved, and on a phone was the wrong instruction six times in one errand
//! (decision 0208).

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::{FrameId, PageEpoch, TabId};
use task_engine::agent::{ModelToolCall, TurnPage};
use task_engine::{
    ArgumentValue, CallVerdict, CurrentDocument, ModelStopReason, NotAttempted, SuppliedArgument,
};

use common::agent::{page, with_recorded_turn_on};

fn link_open(node: u32) -> ModelToolCall {
    ModelToolCall::new(
        "browser.link.open",
        vec![SuppliedArgument::new("node", ArgumentValue::Handle(node))],
    )
}

fn document(tab: &str, frame: &str, epoch: &str) -> CurrentDocument {
    CurrentDocument {
        tab: TabId::new(tab),
        frame: FrameId::new(frame),
        page_epoch: PageEpoch::new(epoch),
    }
}

fn verdict_on(page: TurnPage, node: u32) -> Option<CallVerdict> {
    let (fixture, residency) =
        with_recorded_turn_on(page, ModelStopReason::ToolCall, vec![link_open(node)]);
    fixture
        .reducer
        .turn_dispositions(&residency)
        .first()
        .map(|disposition| disposition.verdict)
}

#[test]
fn a_number_from_a_document_the_tab_has_left_is_refused_on_sight() {
    let (page, handle) = page();
    // The fixture's number was read from `epoch_1`; the tab's root frame now
    // holds another document.
    let moved = page.with_current_documents(vec![document("tab_1", "frame_1", "epoch_2")]);
    assert_eq!(
        verdict_on(moved, handle.value()),
        Some(CallVerdict::NotAttempted(
            NotAttempted::NodeHandleFromAPageTheTabLeft
        ))
    );
}

#[test]
fn a_number_from_the_document_the_tab_still_holds_is_admitted() {
    let (page, handle) = page();
    let same = page.with_current_documents(vec![document("tab_1", "frame_1", "epoch_1")]);
    assert!(!matches!(
        verdict_on(same, handle.value()),
        Some(CallVerdict::NotAttempted(_)) | None
    ));
}

#[test]
fn another_tab_or_frame_moving_does_not_refuse_the_number() {
    let (page, handle) = page();
    let elsewhere = page.with_current_documents(vec![
        document("tab_2", "frame_1", "epoch_9"),
        document("tab_1", "frame_2", "epoch_9"),
    ]);
    assert!(!matches!(
        verdict_on(elsewhere, handle.value()),
        Some(CallVerdict::NotAttempted(_)) | None
    ));
}

/// The two are told apart, and that is the whole of decision 0208.
///
/// A number nobody printed and a number whose page the tab has left both used
/// to answer `handle_unknown`, so a log could not say which had happened and
/// the model got one sentence for both. The sentence suited the first and
/// misdirected the second: it says to use a number from the latest snapshot,
/// which a model that had just read the page has already done.
#[test]
fn a_number_nobody_printed_and_a_number_from_a_left_page_are_different_answers() {
    let (page, handle) = page();
    let moved = page
        .clone()
        .with_current_documents(vec![document("tab_1", "frame_1", "epoch_2")]);
    let from_a_left_page = verdict_on(moved, handle.value());

    // A number this task never issued, on a page that has not moved at all.
    let same = page.with_current_documents(vec![document("tab_1", "frame_1", "epoch_1")]);
    let never_printed = verdict_on(same, handle.value().wrapping_add(4_242));

    assert_eq!(
        never_printed,
        Some(CallVerdict::NotAttempted(NotAttempted::HandleUnknown))
    );
    assert_eq!(
        from_a_left_page,
        Some(CallVerdict::NotAttempted(
            NotAttempted::NodeHandleFromAPageTheTabLeft
        ))
    );
    assert_ne!(never_printed, from_a_left_page);
}
