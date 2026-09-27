// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical typed action intent.
//!
//! A durable intent contains operation shape, closed operands, identifiers,
//! addresses, and opaque references. Model-authored text stays in the turn's
//! bounded residency; [`OpaqueOperandRef`] binds it without retaining it.

mod browser;
mod encoding;
mod library;
mod link;
mod media_tool;
mod memory;
mod store;
mod tool_job;

use bip_types::identity::{FrameId, GraphRevision, PageEpoch, SemanticNodeId, TabId};

pub use self::browser::{BrowserIntent, DisclosureState};
pub use self::library::{
    LibraryIntent, DEFAULT_LIBRARY_SEARCH_RESULTS, MAX_LIBRARY_SEARCH_RESULTS,
};
pub use self::link::{LinkHandleOriginKind, ObservedLinkHandle, ObservedNodeHandle};
pub use self::media_tool::{MediaOperation, MediaToolIntent};
pub use self::memory::{
    MemoryIntent, MemoryScopeIntent, DEFAULT_MEMORY_SEARCH_RESULTS, MAX_MEMORY_SEARCH_RESULTS,
};
pub use self::store::{StoreIntent, StoreKind, DEFAULT_STORE_RESULTS, MAX_STORE_RESULTS};
pub use self::tool_job::{PythonEntrypoint, ToolJobIntent};

use crate::authority::ActionClass;
use crate::task::BrowserSessionId;
use crate::tool::{IdempotencyClass, ToolRuntime};

/// Maximum bytes in the derived handle of one transient operand.
pub const MAX_OPAQUE_OPERAND_HANDLE_BYTES: usize = 96;

/// Maximum bytes in one durable canonical action intent.
pub const MAX_CANONICAL_ACTION_INTENT_BYTES: usize = 16_384;

/// One exact assistant-owned tab and the document observed when its model
/// handle was issued.
///
/// The browser session and full page lifetime tuple make this suitable for a
/// durable intent: recovery can reconcile this exact target, but can never
/// reinterpret a short model handle after either session or document changes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskTabTarget {
    browser_session_id: BrowserSessionId,
    tab_id: TabId,
    frame_id: FrameId,
    page_epoch: PageEpoch,
    graph_revision: GraphRevision,
}

impl TaskTabTarget {
    pub const fn new(
        browser_session_id: BrowserSessionId,
        tab_id: TabId,
        frame_id: FrameId,
        page_epoch: PageEpoch,
        graph_revision: GraphRevision,
    ) -> Self {
        Self {
            browser_session_id,
            tab_id,
            frame_id,
            page_epoch,
            graph_revision,
        }
    }

    pub const fn browser_session_id(&self) -> &BrowserSessionId {
        &self.browser_session_id
    }

    pub const fn tab_id(&self) -> &TabId {
        &self.tab_id
    }

    pub const fn frame_id(&self) -> &FrameId {
        &self.frame_id
    }

    pub const fn page_epoch(&self) -> &PageEpoch {
        &self.page_epoch
    }

    pub const fn graph_revision(&self) -> GraphRevision {
        self.graph_revision
    }
}

/// Why canonical action-intent bytes were refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ActionIntentCodecError {
    TooLarge,
    InvalidPrefix,
    Truncated,
    DuplicateOrOutOfOrderTag,
    UnknownTag,
    MissingField,
    InvalidUtf8,
    InvalidValue,
}

/// Which transient, model-authored operand a reference names.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum OpaqueOperandKind {
    SearchQuery,
    DomQueryText,
    LibraryQuery,
    MemoryQuery,
    MemoryStatement,
    PythonTitle,
    PythonContent,
    StoreQuery,
}

impl OpaqueOperandKind {
    pub(crate) const fn label(self) -> &'static str {
        match self {
            Self::SearchQuery => "search-query",
            Self::DomQueryText => "dom-query-text",
            Self::LibraryQuery => "library-query",
            Self::MemoryQuery => "memory-query",
            Self::MemoryStatement => "memory-statement",
            Self::PythonTitle => "python-title",
            Self::PythonContent => "python-content",
            Self::StoreQuery => "store-query",
        }
    }

    pub(crate) const fn wire_tag(self) -> u8 {
        match self {
            Self::SearchQuery => 0,
            Self::DomQueryText => 1,
            Self::LibraryQuery => 2,
            Self::MemoryQuery => 3,
            Self::MemoryStatement => 4,
            Self::PythonTitle => 5,
            Self::PythonContent => 6,
            Self::StoreQuery => 7,
        }
    }
}

/// Content-free reference to bytes held only by one model-turn residency.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct OpaqueOperandRef {
    handle: String,
    kind: OpaqueOperandKind,
    digest: [u8; 32],
}

impl OpaqueOperandRef {
    /// Derives the only handle format from a frozen turn and call.
    pub fn for_call(
        ordinal: u64,
        sequence: u32,
        kind: OpaqueOperandKind,
        digest: [u8; 32],
    ) -> Self {
        Self {
            handle: format!("turn-{ordinal}-call-{sequence}-{}", kind.label()),
            kind,
            digest,
        }
    }

    pub fn handle(&self) -> &str {
        &self.handle
    }

    pub const fn kind(&self) -> OpaqueOperandKind {
        self.kind
    }

    pub const fn digest(&self) -> &[u8; 32] {
        &self.digest
    }

    pub(crate) fn matches_call(&self, ordinal: u64, sequence: u32) -> bool {
        self.handle == Self::for_call(ordinal, sequence, self.kind, self.digest).handle
    }
}

/// A role accepted by the protocol-bounded DOM query.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DomQueryRole {
    Link,
    Button,
    Field,
    Heading,
    List,
    Table,
    Image,
    Region,
}

impl DomQueryRole {
    pub const fn from_choice(value: &str) -> Option<Self> {
        match value.as_bytes() {
            b"link" => Some(Self::Link),
            b"button" => Some(Self::Button),
            b"field" => Some(Self::Field),
            b"heading" => Some(Self::Heading),
            b"list" => Some(Self::List),
            b"table" => Some(Self::Table),
            b"image" => Some(Self::Image),
            b"region" => Some(Self::Region),
            _ => None,
        }
    }

    pub(crate) const fn wire_tag(self) -> u8 {
        match self {
            Self::Link => 0,
            Self::Button => 1,
            Self::Field => 2,
            Self::Heading => 3,
            Self::List => 4,
            Self::Table => 5,
            Self::Image => 6,
            Self::Region => 7,
        }
    }
}

/// The closed direction of one semantic scroll.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ScrollDirection {
    Up,
    Down,
    ToNode,
}

impl ScrollDirection {
    pub const fn from_choice(value: &str) -> Option<Self> {
        match value.as_bytes() {
            b"up" => Some(Self::Up),
            b"down" => Some(Self::Down),
            b"to_node" => Some(Self::ToNode),
            _ => None,
        }
    }

    pub(crate) const fn wire_tag(self) -> u8 {
        match self {
            Self::Up => 0,
            Self::Down => 1,
            Self::ToNode => 2,
        }
    }
}

/// The one canonical operation carried by an action proposal.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ActionIntent {
    Browser(BrowserIntent),
    Library(LibraryIntent),
    Memory(MemoryIntent),
    Store(StoreIntent),
    MediaTool(MediaToolIntent),
    ToolJob(ToolJobIntent),
}

impl ActionIntent {
    /// Whether this build has the transient ownership/result seam required to
    /// propose the operation.
    pub const fn is_proposable(&self) -> bool {
        !matches!(
            self,
            Self::Browser(
                BrowserIntent::DomRead {
                    target: Some(_),
                    ..
                } | BrowserIntent::Navigate { new_tab: true, .. }
                    | BrowserIntent::TabsOpen { address: None, .. }
                    | BrowserIntent::DomScroll {
                        direction: ScrollDirection::Up | ScrollDirection::Down,
                        ..
                    }
                    | BrowserIntent::DomScroll {
                        direction: ScrollDirection::ToNode,
                        target: None,
                        ..
                    }
            )
        )
    }

    pub fn tool_name(&self) -> &str {
        match self {
            Self::Browser(value) => value.tool_name(),
            Self::Library(value) => value.tool_name(),
            Self::Memory(value) => value.tool_name(),
            Self::Store(value) => value.tool_name(),
            Self::MediaTool(value) => &value.tool_name,
            Self::ToolJob(value) => &value.tool_name,
        }
    }
    pub const fn action_class(&self) -> ActionClass {
        match self {
            Self::Browser(value) => value.action_class(),
            Self::Library(value) => value.action_class(),
            Self::Memory(value) => value.action_class(),
            Self::Store(value) => value.action_class(),
            Self::MediaTool(_) | Self::ToolJob(_) => ActionClass::ExecuteToolJob,
        }
    }
    /// Browser-custodied artifact format produced by this operation, if any.
    pub const fn produced_artifact_kind(&self) -> Option<crate::artifact::ArtifactKind> {
        match self {
            Self::MediaTool(value) => value.operation.artifact_kind(),
            Self::ToolJob(value) => Some(value.entrypoint.artifact_kind()),
            Self::Browser(_) | Self::Library(_) | Self::Memory(_) | Self::Store(_) => None,
        }
    }
    pub const fn tab_id(&self) -> &TabId {
        match self {
            Self::Browser(value) => value.tab_id(),
            Self::Library(value) => value.tab_id(),
            Self::Memory(value) => value.tab_id(),
            Self::Store(value) => value.tab_id(),
            Self::MediaTool(value) => &value.tab,
            Self::ToolJob(value) => &value.tab,
        }
    }
    pub const fn node_id(&self) -> Option<&SemanticNodeId> {
        match self {
            Self::Browser(value) => value.node_id(),
            Self::Library(value) => value.node_id(),
            Self::Memory(value) => value.node_id(),
            Self::Store(value) => value.node_id(),
            Self::MediaTool(value) => value.node.as_ref(),
            Self::ToolJob(value) => value.node.as_ref(),
        }
    }
    pub fn destination_address(&self) -> Option<&str> {
        match self {
            Self::Browser(value) => value.destination_address(),
            Self::Library(_)
            | Self::Memory(_)
            | Self::Store(_)
            | Self::MediaTool(_)
            | Self::ToolJob(_) => None,
        }
    }
    pub const fn idempotency(&self) -> IdempotencyClass {
        match self {
            Self::Browser(value) => value.idempotency(),
            Self::Library(value) => value.idempotency(),
            Self::Memory(value) => value.idempotency(),
            Self::Store(value) => value.idempotency(),
            Self::MediaTool(value) => value.idempotency,
            Self::ToolJob(value) => value.idempotency,
        }
    }
    pub const fn tool_runtime(&self) -> Option<ToolRuntime> {
        match self {
            Self::Browser(_) | Self::Library(_) | Self::Memory(_) | Self::Store(_) => None,
            Self::MediaTool(_) => Some(ToolRuntime::Media),
            Self::ToolJob(value) => Some(value.runtime),
        }
    }
    /// Frozen v1 tagged, length-prefixed durable encoding.
    pub fn encode_canonical(&self) -> Result<Vec<u8>, ActionIntentCodecError> {
        let encoded = encoding::encode(self);
        if encoded.len() > MAX_CANONICAL_ACTION_INTENT_BYTES {
            return Err(ActionIntentCodecError::TooLarge);
        }
        // The strict inverse is also the constructor-side validator. This
        // prevents an in-memory identifier or address the wire would refuse
        // from being persisted as canonical bytes in the first place.
        if encoding::decode(&encoded)? != *self {
            return Err(ActionIntentCodecError::InvalidValue);
        }
        Ok(encoded)
    }

    /// Strict inverse of [`Self::encode_canonical`].
    pub fn decode_canonical(bytes: &[u8]) -> Result<Self, ActionIntentCodecError> {
        encoding::decode(bytes)
    }
}

#[cfg(test)]
mod tests;
