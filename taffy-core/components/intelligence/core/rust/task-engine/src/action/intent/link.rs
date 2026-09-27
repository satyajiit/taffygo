// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact, destination-free identity of a node observed by the browser.

use bip_types::identity::{
    FrameId, GraphRevision, NodeHandle, OriginKind, PageEpoch, SemanticNodeId, TabId,
};

/// Which exact BIP origin representation an observed link handle carries.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LinkHandleOriginKind {
    Tuple,
    Opaque,
}

impl LinkHandleOriginKind {
    pub(crate) const fn wire_tag(self) -> u8 {
        match self {
            Self::Tuple => 0,
            Self::Opaque => 1,
        }
    }
}

/// The complete browser observation behind one model-visible node number.
///
/// This value contains no destination. It preserves the exact tab, frame,
/// document epoch, graph revision, node, and expected origin that the model's
/// number resolved to. Its canonical bytes are only a stale-refusal identity:
/// authority still depends on the browser's transient destination registry,
/// so restoring the bytes after a process restart cannot make the handle live.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ObservedNodeHandle(Box<ObservedNodeHandleFields>);

#[derive(Clone, Debug, PartialEq, Eq)]
struct ObservedNodeHandleFields {
    tab_id: TabId,
    frame_id: FrameId,
    page_epoch: PageEpoch,
    graph_revision: GraphRevision,
    node_id: SemanticNodeId,
    expected_origin_kind: LinkHandleOriginKind,
    expected_origin_value: String,
}

impl ObservedNodeHandle {
    /// Freezes all six identity facts from a BIP node handle.
    ///
    /// Malformed origin unions are refused rather than repaired. The BIP
    /// schema makes tuple serialization and opaque identity mutually
    /// exclusive, and an action reference must retain that distinction.
    pub fn from_node_handle(handle: &NodeHandle) -> Option<Self> {
        let (expected_origin_kind, expected_origin_value) = match handle.expected_origin.kind {
            OriginKind::Tuple => (
                LinkHandleOriginKind::Tuple,
                handle
                    .expected_origin
                    .serialization
                    .as_ref()
                    .filter(|value| {
                        !value.is_empty() && handle.expected_origin.opaque_id.is_none()
                    })?
                    .clone(),
            ),
            OriginKind::Opaque => (
                LinkHandleOriginKind::Opaque,
                handle
                    .expected_origin
                    .opaque_id
                    .as_ref()
                    .filter(|value| {
                        !value.is_empty() && handle.expected_origin.serialization.is_none()
                    })?
                    .clone(),
            ),
        };
        Some(Self::from_parts(
            handle.tab_id.clone(),
            handle.frame_id.clone(),
            handle.page_epoch.clone(),
            handle.graph_revision,
            handle.node_id.clone(),
            expected_origin_kind,
            expected_origin_value,
        ))
    }

    pub const fn tab_id(&self) -> &TabId {
        &self.0.tab_id
    }

    pub const fn frame_id(&self) -> &FrameId {
        &self.0.frame_id
    }

    pub const fn page_epoch(&self) -> &PageEpoch {
        &self.0.page_epoch
    }

    pub const fn graph_revision(&self) -> GraphRevision {
        self.0.graph_revision
    }

    pub const fn node_id(&self) -> &SemanticNodeId {
        &self.0.node_id
    }

    pub const fn expected_origin_kind(&self) -> LinkHandleOriginKind {
        self.0.expected_origin_kind
    }

    pub fn expected_origin_value(&self) -> &str {
        &self.0.expected_origin_value
    }

    pub(super) fn from_parts(
        tab_id: TabId,
        frame_id: FrameId,
        page_epoch: PageEpoch,
        graph_revision: GraphRevision,
        node_id: SemanticNodeId,
        expected_origin_kind: LinkHandleOriginKind,
        expected_origin_value: String,
    ) -> Self {
        Self(Box::new(ObservedNodeHandleFields {
            tab_id,
            frame_id,
            page_epoch,
            graph_revision,
            node_id,
            expected_origin_kind,
            expected_origin_value,
        }))
    }
}

/// Compatibility spelling for the first operation that adopted the complete
/// observed-node identity. New node-targeted operations use
/// [`ObservedNodeHandle`] directly.
pub type ObservedLinkHandle = ObservedNodeHandle;

#[cfg(test)]
mod tests;
