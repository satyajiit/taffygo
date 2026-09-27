// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! `browser.link.open` may name only a line the reading printed as a link
//! that leads somewhere.
//!
//! The browser opens only links whose address it read, and answers any other
//! line as a node that is gone. On the myAadhaar home page a model named a
//! control that leads nowhere, was told to read the page again, named another
//! like it, and the errand ended with nothing. Refused on sight, it is told
//! to press the control instead (decision 0241).

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::{
    FrameId, GraphRevision, NodeHandle, Origin, OriginKind, PageEpoch, SemanticNodeId, TabId,
};
use bip_types::snapshot::Sensitivity;
use task_engine::agent::{ModelToolCall, TurnPage, TurnResidency};
use task_engine::{
    ArgumentValue, CallVerdict, Command, HandleTable, Milestone, ModelStopReason, NotAttempted,
    PageReadability, RenderShape, SuppliedArgument, ValueTarget,
};

use common::agent::reply;

fn node(node_id: &str) -> NodeHandle {
    NodeHandle::new(
        TabId::new("tab_1"),
        FrameId::new("frame_1"),
        PageEpoch::new("epoch_1"),
        GraphRevision(3),
        SemanticNodeId::new(node_id),
        Origin {
            kind: OriginKind::Tuple,
            serialization: Some("https://example.test".to_owned()),
            opaque_id: None,
        },
    )
}

/// A page whose one line was printed as a link that leads somewhere, or not.
fn page_with_one_line(opens_as_link: bool) -> (TurnPage, u32) {
    let mut handles = HandleTable::new();
    let handle = handles
        .issue_rendered(
            node("n-1"),
            ValueTarget::None,
            Sensitivity::NotSensitive,
            opens_as_link,
        )
        .expect("an empty table issues a number");
    let shape = RenderShape {
        readability: PageReadability::Readable,
        offered_nodes: 1,
        omitted_nodes: 0,
        unreadable_nodes: 0,
        unreadable_text_bytes: 0,
        digest: [5_u8; 32],
    };
    (
        TurnPage::new(TabId::new("tab_1"), handles, shape),
        handle.value(),
    )
}

fn verdict_of_opening(page: TurnPage, node: u32) -> Option<CallVerdict> {
    let mut seed = common::seed();
    seed.snapshot.milestone = Milestone::M5;
    seed.snapshot
        .tool_allowlist
        .push("browser.link.open".to_owned());
    let mut fixture = common::draft_from(seed);
    fixture.must_apply(Command::StartTask(common::preview()));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let call = ModelToolCall::new(
        "browser.link.open",
        vec![SuppliedArgument::new("node", ArgumentValue::Handle(node))],
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
fn opening_a_line_that_leads_nowhere_is_refused_on_sight() {
    let (page, handle) = page_with_one_line(false);
    assert_eq!(
        verdict_of_opening(page, handle),
        Some(CallVerdict::NotAttempted(NotAttempted::NotALink))
    );
}

#[test]
fn opening_a_link_that_leads_somewhere_is_not_refused_on_sight() {
    let (page, handle) = page_with_one_line(true);
    assert!(!matches!(
        verdict_of_opening(page, handle),
        Some(CallVerdict::NotAttempted(NotAttempted::NotALink)) | None
    ));
}

#[test]
fn a_number_issued_without_a_rendered_line_is_left_to_the_browser() {
    let mut handles = HandleTable::new();
    let marked = handles
        .issue_marked(node("n-1"), ValueTarget::None)
        .expect("a number");
    let classified = handles
        .issue_classified(node("n-2"), ValueTarget::None, Sensitivity::NotSensitive)
        .expect("a number");
    assert!(handles.opens_as_link(marked.value()));
    assert!(handles.opens_as_link(classified.value()));
    assert!(!handles.opens_as_link(u32::MAX));
}
