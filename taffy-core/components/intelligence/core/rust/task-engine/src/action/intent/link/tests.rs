// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use bip_types::identity::{Origin, SemanticNodeId};

use crate::action::{ActionIntent, BrowserIntent};

fn node(origin: Origin) -> NodeHandle {
    NodeHandle::new(
        TabId::new("tab"),
        FrameId::new("frame"),
        PageEpoch::new("epoch"),
        GraphRevision(1),
        SemanticNodeId::new("node"),
        origin,
    )
}

fn exact_handle(
    tab_id: &str,
    frame_id: &str,
    page_epoch: &str,
    graph_revision: u64,
    node_id: &str,
    origin_kind: LinkHandleOriginKind,
    origin_value: &str,
) -> ObservedLinkHandle {
    ObservedLinkHandle::from_parts(
        TabId::new(tab_id),
        FrameId::new(frame_id),
        PageEpoch::new(page_epoch),
        GraphRevision(graph_revision),
        SemanticNodeId::new(node_id),
        origin_kind,
        origin_value.to_owned(),
    )
}

fn canonical(handle: ObservedLinkHandle) -> Vec<u8> {
    ActionIntent::Browser(BrowserIntent::LinkOpen { target: handle })
        .encode_canonical()
        .expect("valid observed link handle")
}

#[test]
fn malformed_origin_unions_never_become_link_handles() {
    for origin in [
        Origin {
            kind: OriginKind::Tuple,
            serialization: None,
            opaque_id: None,
        },
        Origin {
            kind: OriginKind::Tuple,
            serialization: Some("https://example.test".to_owned()),
            opaque_id: Some("opaque".to_owned()),
        },
        Origin {
            kind: OriginKind::Opaque,
            serialization: Some("https://example.test".to_owned()),
            opaque_id: Some("opaque".to_owned()),
        },
        Origin {
            kind: OriginKind::Opaque,
            serialization: None,
            opaque_id: None,
        },
    ] {
        assert_eq!(ObservedLinkHandle::from_node_handle(&node(origin)), None);
    }
}

#[test]
fn every_scope_fact_changes_canonical_material() {
    let base = canonical(exact_handle(
        "tab",
        "frame",
        "epoch",
        7,
        "node",
        LinkHandleOriginKind::Tuple,
        "https://example.test",
    ));
    for changed in [
        exact_handle(
            "other-tab",
            "frame",
            "epoch",
            7,
            "node",
            LinkHandleOriginKind::Tuple,
            "https://example.test",
        ),
        exact_handle(
            "tab",
            "other-frame",
            "epoch",
            7,
            "node",
            LinkHandleOriginKind::Tuple,
            "https://example.test",
        ),
        exact_handle(
            "tab",
            "frame",
            "other-epoch",
            7,
            "node",
            LinkHandleOriginKind::Tuple,
            "https://example.test",
        ),
        exact_handle(
            "tab",
            "frame",
            "epoch",
            8,
            "node",
            LinkHandleOriginKind::Tuple,
            "https://example.test",
        ),
        exact_handle(
            "tab",
            "frame",
            "epoch",
            7,
            "other-node",
            LinkHandleOriginKind::Tuple,
            "https://example.test",
        ),
        exact_handle(
            "tab",
            "frame",
            "epoch",
            7,
            "node",
            LinkHandleOriginKind::Opaque,
            "opaque-1",
        ),
        exact_handle(
            "tab",
            "frame",
            "epoch",
            7,
            "node",
            LinkHandleOriginKind::Tuple,
            "https://other.test",
        ),
    ] {
        assert_ne!(base, canonical(changed));
    }
}
