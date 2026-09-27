// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A move naming a number on the person's page is not made once the task has
//! a tab of its own (decision 0237).
//!
//! Numbers are task-global, so a number printed for the page an errand was
//! asked from stays resolvable for the life of the task. On a phone the model,
//! long after it had moved into its own tab, named the person's "Learn more"
//! link in a `browser.link.open`. `call_target` took the tab from the node, the
//! node was on the person's page, and the person's own tab was navigated away:
//! the browser then had no live task tab to issue a source for, and the
//! errand's consent went with it.
//!
//! These drive the reducer end to end, so the person's page is the one the
//! task's own durable record says the person attached, and not a fact the test
//! hands the call.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::{
    FrameId, GraphRevision, NodeHandle, Origin, OriginKind, PageEpoch, SemanticNodeId, TabId,
};
use task_engine::agent::{ModelToolCall, TurnPage, TurnResidency};
use task_engine::{
    ArgumentValue, CallVerdict, Command, HandleTable, ModelHandle, ModelStopReason, NotAttempted,
    PageReadability, RenderShape, SuppliedArgument, ValueTarget,
};

use common::agent::{record_turn_in, Digest};
use common::errand::{prepared_errand_admitting, running_errand_from_a_page, DISCOVERY_TAB};

/// The page the person was on when they asked, as `common::preview` binds it.
const PERSONS_TAB: &str = "tab_1";

const TOOLS: &[&str] = &[
    "browser.link.open",
    "browser.dom.click",
    "browser.dom.query",
    "user.request_values",
];

/// A projection that printed one number, for a node standing in `tab`, while
/// the task knows the blank tab the browser opened for it.
fn page_with_a_node_on(tab: &str) -> (TurnPage, ModelHandle) {
    let mut handles = HandleTable::new();
    let handle = handles
        .issue_marked(
            NodeHandle::new(
                TabId::new(tab),
                FrameId::new("frame_1"),
                PageEpoch::new("epoch_1"),
                GraphRevision(4),
                SemanticNodeId::new("n-learn-more"),
                Origin {
                    kind: OriginKind::Tuple,
                    serialization: Some("https://example.test".to_owned()),
                    opaque_id: None,
                },
            ),
            ValueTarget::Container,
        )
        .expect("an empty table issues one number");
    let shape = RenderShape {
        readability: PageReadability::Readable,
        offered_nodes: 1,
        omitted_nodes: 0,
        unreadable_nodes: 0,
        unreadable_text_bytes: 0,
        digest: [7_u8; 32],
    };
    let page = TurnPage::new(TabId::new(PERSONS_TAB), handles, shape)
        .with_discovery_tab(Some(TabId::new(DISCOVERY_TAB)));
    (page, handle)
}

fn naming(tool: &str, parameter: &str, handle: ModelHandle) -> ModelToolCall {
    ModelToolCall::new(
        tool,
        vec![SuppliedArgument::new(
            parameter,
            ArgumentValue::Handle(handle.value()),
        )],
    )
}

/// What became of the one call, and the command the walk answers next.
fn after(
    fixture: &mut common::Fixture,
    page: TurnPage,
    call: ModelToolCall,
) -> (CallVerdict, Option<Command>) {
    let residency: TurnResidency =
        record_turn_in(fixture, page, ModelStopReason::ToolCall, vec![call]);
    let verdict = fixture
        .reducer
        .turn_dispositions(&residency)
        .first()
        .expect("the reply carried one call")
        .verdict;
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .expect("a refused call is not a refused walk");
    (verdict, next)
}

/// The phone's own call: a link-open naming the person's "Learn more" after
/// the task has a tab of its own. It is refused, and nothing is proposed —
/// and so is a press, and a request for values that would begin filling the
/// person's page.
#[test]
fn a_move_naming_a_number_on_the_persons_page_is_not_made() {
    for (tool, parameter) in [
        ("browser.link.open", "node"),
        ("browser.dom.click", "node"),
        ("user.request_values", "form"),
    ] {
        let mut fixture = running_errand_from_a_page(8, TOOLS);
        let (page, handle) = page_with_a_node_on(PERSONS_TAB);
        let (verdict, next) = after(&mut fixture, page, naming(tool, parameter, handle));
        assert_eq!(
            verdict,
            CallVerdict::NotAttempted(NotAttempted::NodeOnThePersonsPage),
            "{tool}"
        );
        // Nothing is proposed: the walk goes straight to the next turn, which
        // is where the model reads why.
        assert!(
            matches!(next, Some(Command::RequestModelTurn { .. })),
            "{tool} on the person's page was not simply refused: {next:?}"
        );
    }
}

/// A read naming the same number still reads the person's page. That is what
/// decision 0225 keeps, and this record does not take it away.
#[test]
fn a_read_naming_a_number_on_the_persons_page_still_reads_it() {
    let mut fixture = running_errand_from_a_page(8, TOOLS);
    let (page, handle) = page_with_a_node_on(PERSONS_TAB);
    let (verdict, next) = after(
        &mut fixture,
        page,
        naming("browser.dom.query", "within", handle),
    );
    assert_eq!(verdict, CallVerdict::Attemptable);
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected the read to be proposed, got {next:?}");
    };
    assert_eq!(proposal.tab_id(), &TabId::new(PERSONS_TAB));
}

/// A move naming a number in the task's own tab is made there, as before.
#[test]
fn a_move_naming_a_number_in_the_tasks_own_tab_is_made() {
    let mut fixture = running_errand_from_a_page(8, TOOLS);
    let (page, handle) = page_with_a_node_on(DISCOVERY_TAB);
    let (verdict, next) = after(
        &mut fixture,
        page,
        naming("browser.link.open", "node", handle),
    );
    assert_eq!(verdict, CallVerdict::Attemptable);
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected the move to be proposed, got {next:?}");
    };
    assert_eq!(proposal.tab_id(), &TabId::new(DISCOVERY_TAB));
}

/// A zero-source errand never held a page of the person's, so every tab it
/// acts in is one it owns and nothing is set apart — here a tab it opened
/// for itself beside the prepared one.
#[test]
fn a_zero_source_errand_is_unaffected() {
    let mut fixture = prepared_errand_admitting(8, "browser.link.open");
    let (page, handle) = page_with_a_node_on("tab_9");
    let (verdict, next) = after(
        &mut fixture,
        page,
        naming("browser.link.open", "node", handle),
    );
    assert_eq!(verdict, CallVerdict::Attemptable);
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected the move to be proposed, got {next:?}");
    };
    assert_eq!(proposal.tab_id(), &TabId::new("tab_9"));
}
