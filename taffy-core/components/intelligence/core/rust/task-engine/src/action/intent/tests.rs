// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod refusals;

use super::*;
use crate::{BrowserSessionId, FieldValueRequestId};
use bip_types::identity::{FrameId, GraphRevision, PageEpoch};

fn tab(value: &str) -> TabId {
    TabId::new(value)
}

fn node(value: &str) -> SemanticNodeId {
    SemanticNodeId::new(value)
}

fn browser_session() -> BrowserSessionId {
    BrowserSessionId::new("browser-session-1").unwrap_or_else(|_| unreachable!())
}

fn value_request(value: &str) -> FieldValueRequestId {
    FieldValueRequestId::new(value).unwrap_or_else(|_| unreachable!())
}

fn task_tab(value: &str) -> TaskTabTarget {
    TaskTabTarget::new(
        browser_session(),
        tab(value),
        FrameId::new("tab-frame"),
        PageEpoch::new("tab-epoch"),
        GraphRevision(9),
    )
}

fn opaque(kind: OpaqueOperandKind, byte: u8) -> OpaqueOperandRef {
    OpaqueOperandRef::for_call(3, 4, kind, [byte; 32])
}

fn link_handle(tab_id: &str, node_id: &str) -> ObservedLinkHandle {
    ObservedLinkHandle::from_parts(
        tab(tab_id),
        FrameId::new("frame"),
        PageEpoch::new("epoch"),
        GraphRevision(7),
        node(node_id),
        LinkHandleOriginKind::Tuple,
        "https://example.test".to_owned(),
    )
}

fn browser(value: BrowserIntent) -> ActionIntent {
    ActionIntent::Browser(value)
}

fn python_job() -> ActionIntent {
    ActionIntent::ToolJob(ToolJobIntent {
        tool_name: "python.execute".to_owned(),
        runtime: ToolRuntime::Python,
        entrypoint: super::PythonEntrypoint::DocumentBuild,
        title: opaque(OpaqueOperandKind::PythonTitle, 4),
        content: opaque(OpaqueOperandKind::PythonContent, 5),
        tab: tab("t"),
        node: Some(node("n")),
        idempotency: IdempotencyClass::Consequential,
    })
}

fn all_intents() -> Vec<ActionIntent> {
    let mut intents = browser_intents();
    intents.extend(domain_intents());
    intents.extend(download_intents());
    intents
}

fn download_intents() -> Vec<ActionIntent> {
    vec![
        browser(BrowserIntent::DownloadStart {
            tab: tab("t"),
            address: "https://example.test/file".to_owned(),
            browser_session_id: browser_session(),
        }),
        browser(BrowserIntent::DownloadFromLink {
            target: link_handle("t", "download-link"),
            browser_session_id: browser_session(),
        }),
        browser(BrowserIntent::DownloadList {
            tab: tab("t"),
            browser_session_id: browser_session(),
        }),
        browser(BrowserIntent::DownloadCancel {
            tab: tab("t"),
            browser_session_id: browser_session(),
            download_id: "opaque-download-guid".to_owned(),
        }),
    ]
}

fn browser_intents() -> Vec<ActionIntent> {
    vec![
        browser(BrowserIntent::Navigate {
            tab: tab("t"),
            address: "https://example.test".to_owned(),
            new_tab: true,
        }),
        browser(BrowserIntent::Search {
            tab: tab("t"),
            query: opaque(OpaqueOperandKind::SearchQuery, 1),
        }),
        browser(BrowserIntent::HistoryBack { tab: tab("t") }),
        browser(BrowserIntent::HistoryForward { tab: tab("t") }),
        browser(BrowserIntent::TabsOpen {
            context: tab("t"),
            address: Some("https://example.test/a".to_owned()),
        }),
        browser(BrowserIntent::TabsList {
            context: tab("t"),
            browser_session_id: browser_session(),
        }),
        browser(BrowserIntent::TabsActivate {
            context: tab("t"),
            target: task_tab("u"),
        }),
        browser(BrowserIntent::TabsClose {
            context: tab("t"),
            target: task_tab("u"),
        }),
        browser(BrowserIntent::DomQuery {
            tab: tab("t"),
            within: Some(node("n")),
            role: Some(DomQueryRole::Button),
            text: Some(opaque(OpaqueOperandKind::DomQueryText, 2)),
            limit: Some(7),
        }),
        browser(BrowserIntent::DomRead {
            tab: tab("t"),
            target: Some(node("n")),
        }),
        browser(BrowserIntent::DomClick {
            target: link_handle("t", "n"),
            expected_state: Some(DisclosureState::Expanded),
        }),
        browser(BrowserIntent::DomFocus {
            target: link_handle("t", "focus"),
        }),
        browser(BrowserIntent::LinkOpen {
            target: link_handle("t", "link"),
        }),
        browser(BrowserIntent::DomScroll {
            tab: tab("t"),
            direction: ScrollDirection::ToNode,
            target: Some(node("n")),
        }),
        browser(BrowserIntent::FormInspect {
            tab: tab("t"),
            form: node("f"),
        }),
        browser(BrowserIntent::FormFill {
            tab: tab("t"),
            field: node("f"),
            value_request: value_request("values-1"),
            value_from: 2,
        }),
        browser(BrowserIntent::FormSubmit {
            tab: tab("t"),
            control: node("f"),
        }),
        browser(BrowserIntent::SelectionRead { tab: tab("t") }),
        browser(BrowserIntent::ImageDescribe {
            tab: tab("t"),
            target: node("n"),
        }),
        browser(BrowserIntent::ImageReadText {
            tab: tab("t"),
            target: node("n"),
        }),
        browser(BrowserIntent::VideoInspect {
            tab: tab("t"),
            target: node("n"),
        }),
        browser(BrowserIntent::PdfInspect { tab: tab("t") }),
        browser(BrowserIntent::PageScreenshotInspect { tab: tab("t") }),
    ]
}

fn domain_intents() -> Vec<ActionIntent> {
    vec![
        python_job(),
        ActionIntent::Store(StoreIntent::HistorySearch {
            tab: tab("t"),
            query: opaque(OpaqueOperandKind::StoreQuery, 6),
            limit: 5,
        }),
        ActionIntent::Store(StoreIntent::HistoryRecent {
            tab: tab("t"),
            limit: 32,
        }),
        ActionIntent::Store(StoreIntent::BookmarksSearch {
            tab: tab("t"),
            query: opaque(OpaqueOperandKind::StoreQuery, 7),
            limit: 1,
        }),
        ActionIntent::Store(StoreIntent::BookmarksList {
            tab: tab("t"),
            limit: 10,
        }),
        ActionIntent::Store(StoreIntent::OpenTabsList { tab: tab("t") }),
    ]
}

#[test]
fn every_intent_round_trips_the_frozen_v1_encoding() {
    for intent in all_intents() {
        let bytes = intent.encode_canonical().expect("valid intent");
        assert!(bytes.starts_with(encoding::PREFIX));
        assert_eq!(ActionIntent::decode_canonical(&bytes), Ok(intent));
    }
}

#[test]
fn an_observed_download_keeps_exact_node_and_manager_identity_without_an_address() {
    let intent = browser(BrowserIntent::DownloadFromLink {
        target: link_handle("t", "download-link"),
        browser_session_id: browser_session(),
    });
    assert_eq!(intent.tool_name(), "browser.download.from_link");
    assert_eq!(intent.action_class(), crate::ActionClass::StartDownload);
    assert_eq!(intent.idempotency(), crate::IdempotencyClass::Consequential);
    let ActionIntent::Browser(operation) = &intent else {
        unreachable!()
    };
    assert!(operation.destination_address().is_none());
    assert_eq!(
        operation.node_id().map(SemanticNodeId::as_str),
        Some("download-link")
    );
    let encoded = intent.encode_canonical().unwrap_or_else(|_| unreachable!());
    assert_eq!(ActionIntent::decode_canonical(&encoded), Ok(intent.clone()));
    let other_session = browser(BrowserIntent::DownloadFromLink {
        target: link_handle("t", "download-link"),
        browser_session_id: BrowserSessionId::new("browser-session-2")
            .unwrap_or_else(|_| unreachable!()),
    });
    assert_ne!(intent.encode_canonical(), other_session.encode_canonical());
}

#[test]
fn every_semantic_operand_changes_the_canonical_material() {
    let pairs = [
        (
            browser(BrowserIntent::Navigate {
                tab: tab("t"),
                address: "https://example.test/a".to_owned(),
                new_tab: false,
            }),
            browser(BrowserIntent::Navigate {
                tab: tab("u"),
                address: "https://example.test/b".to_owned(),
                new_tab: true,
            }),
        ),
        (
            browser(BrowserIntent::Search {
                tab: tab("t"),
                query: opaque(OpaqueOperandKind::SearchQuery, 1),
            }),
            browser(BrowserIntent::Search {
                tab: tab("t"),
                query: opaque(OpaqueOperandKind::SearchQuery, 2),
            }),
        ),
        (
            browser(BrowserIntent::TabsOpen {
                context: tab("t"),
                address: None,
            }),
            browser(BrowserIntent::TabsOpen {
                context: tab("u"),
                address: Some("https://example.test".to_owned()),
            }),
        ),
        (
            browser(BrowserIntent::DomQuery {
                tab: tab("t"),
                within: None,
                role: None,
                text: None,
                limit: None,
            }),
            browser(BrowserIntent::DomQuery {
                tab: tab("t"),
                within: Some(node("n")),
                role: Some(DomQueryRole::Region),
                text: Some(opaque(OpaqueOperandKind::DomQueryText, 3)),
                limit: Some(4),
            }),
        ),
        (
            browser(BrowserIntent::DomScroll {
                tab: tab("t"),
                direction: ScrollDirection::Down,
                target: None,
            }),
            browser(BrowserIntent::DomScroll {
                tab: tab("t"),
                direction: ScrollDirection::ToNode,
                target: Some(node("n")),
            }),
        ),
        (
            browser(BrowserIntent::FormFill {
                tab: tab("t"),
                field: node("a"),
                value_request: value_request("values-1"),
                value_from: 0,
            }),
            browser(BrowserIntent::FormFill {
                tab: tab("t"),
                field: node("b"),
                value_request: value_request("values-2"),
                value_from: 1,
            }),
        ),
        (
            browser(BrowserIntent::LinkOpen {
                target: link_handle("t", "link-a"),
            }),
            browser(BrowserIntent::LinkOpen {
                target: link_handle("t", "link-b"),
            }),
        ),
    ];
    for (left, right) in pairs {
        assert_ne!(left.encode_canonical(), right.encode_canonical());
    }
}
