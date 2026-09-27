// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A proposal derives its authority and recovery classes from one typed intent.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod common;

use bip_types::identity::{FrameId, GraphRevision, PageEpoch, SemanticNodeId, TabId};
use task_engine::action::{
    ActionIntent, BrowserIntent, DisclosureState, ObservedNodeHandle, TaskTabTarget,
};
use task_engine::command::Command;
use task_engine::tool::{resolve, Milestone};

fn tab() -> TabId {
    TabId::new("tab_1")
}

fn node() -> SemanticNodeId {
    SemanticNodeId::new("node_1")
}

fn browser_session() -> task_engine::BrowserSessionId {
    task_engine::BrowserSessionId::new("browser-session-1").unwrap_or_else(|_| unreachable!())
}

fn task_tab() -> TaskTabTarget {
    TaskTabTarget::new(
        browser_session(),
        TabId::new("tab_target"),
        FrameId::new("frame_target"),
        PageEpoch::new("epoch_target"),
        GraphRevision(4),
    )
}

fn representative_intents() -> Vec<BrowserIntent> {
    vec![
        BrowserIntent::Navigate {
            tab: tab(),
            address: "https://example.test".to_owned(),
            new_tab: false,
        },
        BrowserIntent::HistoryBack { tab: tab() },
        BrowserIntent::HistoryForward { tab: tab() },
        BrowserIntent::DomQuery {
            tab: tab(),
            within: Some(node()),
            role: None,
            text: None,
            limit: Some(3),
        },
        BrowserIntent::DomRead {
            tab: tab(),
            target: None,
        },
        BrowserIntent::DomClick {
            target: ObservedNodeHandle::from_node_handle(&common::agent::node("node_1"))
                .unwrap_or_else(|| unreachable!()),
            expected_state: Some(DisclosureState::Expanded),
        },
        BrowserIntent::DomFocus {
            target: ObservedNodeHandle::from_node_handle(&common::agent::node("focus_1"))
                .unwrap_or_else(|| unreachable!()),
        },
        BrowserIntent::DomScroll {
            tab: tab(),
            direction: task_engine::action::ScrollDirection::ToNode,
            target: Some(node()),
        },
        BrowserIntent::FormInspect {
            tab: tab(),
            form: node(),
        },
        BrowserIntent::SelectionRead { tab: tab() },
        BrowserIntent::TabsList {
            context: tab(),
            browser_session_id: browser_session(),
        },
        BrowserIntent::TabsActivate {
            context: tab(),
            target: task_tab(),
        },
        BrowserIntent::TabsClose {
            context: tab(),
            target: task_tab(),
        },
    ]
}

#[test]
fn every_constructible_m3_intent_derives_the_exact_registry_classes() {
    for browser in representative_intents() {
        let intent = ActionIntent::Browser(browser);
        let entry = resolve(intent.tool_name(), Milestone::M3)
            .entry()
            .expect("the intent names one exact registry row");
        assert_eq!(entry.idempotency, intent.idempotency(), "{}", entry.name);
        assert_eq!(entry.dispatch.action_class(), Some(intent.action_class()));
    }
}

#[test]
fn unavailable_or_unverifiable_intents_are_never_proposable() {
    for browser in [
        BrowserIntent::DomRead {
            tab: tab(),
            target: Some(node()),
        },
        BrowserIntent::DomScroll {
            tab: tab(),
            direction: task_engine::action::ScrollDirection::Down,
            target: None,
        },
        BrowserIntent::Navigate {
            tab: tab(),
            address: "https://example.test/new".to_owned(),
            new_tab: true,
        },
        BrowserIntent::TabsOpen {
            context: tab(),
            address: None,
        },
    ] {
        let mut fixture = common::running();
        let proposal = common::proposal_for(browser, "tab_result_missing");
        let envelope = fixture.envelope(Command::ProposeAction(Box::new(proposal)));
        assert!(fixture.reducer.apply(envelope).is_err());
    }
}
