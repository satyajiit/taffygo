// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::identity::{SemanticNodeId, TabId};

use super::{
    DomQueryRole, ObservedLinkHandle, ObservedNodeHandle, OpaqueOperandRef, ScrollDirection,
    TaskTabTarget,
};
use crate::authority::ActionClass;
use crate::field_values::FieldValueRequestId;
use crate::task::BrowserSessionId;
use crate::tool::IdempotencyClass;

/// Every browser operation an action proposal can currently name.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum BrowserIntent {
    Navigate {
        tab: TabId,
        address: String,
        new_tab: bool,
    },
    Search {
        tab: TabId,
        query: OpaqueOperandRef,
    },
    HistoryBack {
        tab: TabId,
    },
    HistoryForward {
        tab: TabId,
    },
    Reload {
        tab: TabId,
    },
    StopLoading {
        tab: TabId,
    },
    TabsOpen {
        context: TabId,
        address: Option<String>,
    },
    TabsList {
        context: TabId,
        browser_session_id: BrowserSessionId,
    },
    TabsActivate {
        context: TabId,
        target: TaskTabTarget,
    },
    TabsClose {
        context: TabId,
        target: TaskTabTarget,
    },
    DomQuery {
        tab: TabId,
        within: Option<SemanticNodeId>,
        role: Option<DomQueryRole>,
        text: Option<OpaqueOperandRef>,
        limit: Option<u64>,
    },
    DomRead {
        tab: TabId,
        target: Option<SemanticNodeId>,
    },
    /// Presses one exact observed node.
    ///
    /// `expected_state` is the narrow case: a disclosure control whose exact
    /// expanded or collapsed result the browser can require before dispatch
    /// and verify after it. With no state named this is an ordinary press,
    /// and the browser claims only that the document moved past the dispatch
    /// — a page's own control decides what a press does, and requiring a
    /// disclosure state of every one of them is why a sign-in button could
    /// not be pressed at all.
    DomClick {
        target: ObservedNodeHandle,
        expected_state: Option<DisclosureState>,
    },
    /// Moves focus to one exact node and verifies the fixed focused state.
    DomFocus {
        target: ObservedNodeHandle,
    },
    /// Opens the exact link destination the browser observed for `target`.
    ///
    /// The durable intent carries no address. The browser owns the transient
    /// node-to-destination binding and re-resolves it against the live document
    /// before navigation.
    LinkOpen {
        target: ObservedLinkHandle,
    },
    DomScroll {
        tab: TabId,
        direction: ScrollDirection,
        target: Option<SemanticNodeId>,
    },
    FormInspect {
        tab: TabId,
        form: SemanticNodeId,
    },
    FormFill {
        tab: TabId,
        field: SemanticNodeId,
        /// Browser/core-generated request whose zero-based answer is named.
        value_request: FieldValueRequestId,
        value_from: u32,
    },
    FormSelect {
        tab: TabId,
        field: SemanticNodeId,
        /// Browser/core-generated request whose zero-based answer is named.
        value_request: FieldValueRequestId,
        value_from: u32,
    },
    FormToggle {
        tab: TabId,
        field: SemanticNodeId,
        checked: bool,
    },
    FormSubmit {
        tab: TabId,
        control: SemanticNodeId,
    },
    DownloadStart {
        tab: TabId,
        address: String,
        browser_session_id: BrowserSessionId,
    },
    /// Starts a download from an exact live link. The browser resolves its
    /// destination; no signed address enters the durable intent.
    DownloadFromLink {
        target: ObservedLinkHandle,
        browser_session_id: BrowserSessionId,
    },
    DownloadList {
        tab: TabId,
        browser_session_id: BrowserSessionId,
    },
    DownloadCancel {
        tab: TabId,
        browser_session_id: BrowserSessionId,
        /// Browser-owned GUID resolved from a short task-started handle.
        download_id: String,
    },
    SelectionRead {
        tab: TabId,
    },
    ImageDescribe {
        tab: TabId,
        target: SemanticNodeId,
    },
    ImageReadText {
        tab: TabId,
        target: SemanticNodeId,
    },
    VideoInspect {
        tab: TabId,
        target: SemanticNodeId,
    },
    PdfInspect {
        tab: TabId,
    },
    /// Reads one browser-redacted visible-viewport image only when fresh
    /// structured adapters cannot supply usable page content.
    PageScreenshotInspect {
        tab: TabId,
    },
}

/// The exact reversible disclosure state a DOM activation must reach.
///
/// This is deliberately narrower than BIP's `NodeState`: checked/selected
/// controls have dedicated write classes, focus has `MoveFocus`, and a broad
/// state list would let a synthetic click borrow those classes' authority.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DisclosureState {
    Expanded,
    Collapsed,
}

impl DisclosureState {
    /// The tag an activation with no disclosure claim carries.
    ///
    /// Appended as a third value rather than reshaping field 8 into one of
    /// the optional encodings: this intent is journaled and replayed, so a
    /// field that changes shape strands every record already written. An
    /// older reader meets this value and refuses, which is the closed
    /// enumeration rule and not a compatibility accident.
    pub(crate) const NONE_WIRE_TAG: u8 = 2;

    pub const fn from_choice(value: &str) -> Option<Self> {
        match value.as_bytes() {
            b"expanded" => Some(Self::Expanded),
            b"collapsed" => Some(Self::Collapsed),
            _ => None,
        }
    }

    /// The state a present tag names. `NONE_WIRE_TAG` answers `None` here the
    /// same way an unknown tag does, so a caller reading a wire byte tests for
    /// the absent tag first and treats everything left over as invalid.
    pub(crate) const fn from_wire_tag(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::Expanded),
            1 => Some(Self::Collapsed),
            _ => None,
        }
    }

    pub(crate) const fn wire_tag_of(value: Option<Self>) -> u8 {
        match value {
            Some(Self::Expanded) => 0,
            Some(Self::Collapsed) => 1,
            None => Self::NONE_WIRE_TAG,
        }
    }
}

impl BrowserIntent {
    pub const fn tool_name(&self) -> &'static str {
        match self {
            Self::Navigate { .. } => "browser.navigate",
            Self::Search { .. } => "browser.search",
            Self::HistoryBack { .. } => "browser.back",
            Self::HistoryForward { .. } => "browser.forward",
            Self::Reload { .. } => "browser.reload",
            Self::StopLoading { .. } => "browser.stop_loading",
            Self::TabsOpen { .. } => "browser.tabs.open",
            Self::TabsList { .. } => "browser.tabs.list",
            Self::TabsActivate { .. } => "browser.tabs.activate",
            Self::TabsClose { .. } => "browser.tabs.close",
            Self::DomQuery { .. } => "browser.dom.query",
            Self::DomRead { .. } => "browser.dom.read",
            Self::DomClick { .. } => "browser.dom.click",
            Self::DomFocus { .. } => "browser.dom.focus",
            Self::LinkOpen { .. } => "browser.link.open",
            Self::DomScroll { .. } => "browser.dom.scroll",
            Self::FormInspect { .. } => "browser.form.inspect",
            Self::FormFill { .. } => "browser.form.fill",
            Self::FormSelect { .. } => "browser.form.select",
            Self::FormToggle { .. } => "browser.form.toggle",
            Self::FormSubmit { .. } => "browser.form.submit",
            Self::DownloadStart { .. } => "browser.download.start",
            Self::DownloadFromLink { .. } => "browser.download.from_link",
            Self::DownloadList { .. } => "browser.download.list",
            Self::DownloadCancel { .. } => "browser.download.cancel",
            Self::SelectionRead { .. } => "browser.selection.read",
            Self::ImageDescribe { .. } => "page.images.describe",
            Self::ImageReadText { .. } => "page.images.read_text",
            Self::VideoInspect { .. } => "page.video.inspect",
            Self::PdfInspect { .. } => "page.pdf.inspect",
            Self::PageScreenshotInspect { .. } => "page.screenshot.inspect",
        }
    }

    pub const fn action_class(&self) -> ActionClass {
        match self {
            Self::DomQuery { .. }
            | Self::DomRead { .. }
            | Self::TabsList { .. }
            | Self::FormInspect { .. }
            | Self::DownloadList { .. }
            | Self::SelectionRead { .. }
            | Self::ImageDescribe { .. }
            | Self::ImageReadText { .. }
            | Self::VideoInspect { .. }
            | Self::PdfInspect { .. }
            | Self::PageScreenshotInspect { .. } => ActionClass::ObservePage,
            Self::DomScroll { .. } => ActionClass::ScrollIntoView,
            Self::Navigate { .. } | Self::Search { .. } | Self::LinkOpen { .. } => {
                ActionClass::OpenLink
            }
            Self::HistoryBack { .. }
            | Self::HistoryForward { .. }
            | Self::Reload { .. }
            | Self::StopLoading { .. } => ActionClass::ControlTab,
            Self::TabsOpen { .. } | Self::TabsClose { .. } => ActionClass::CreateTaskTab,
            Self::TabsActivate { .. } | Self::DomFocus { .. } => ActionClass::MoveFocus,
            Self::DomClick { .. } => ActionClass::SyntheticClick,
            Self::FormFill { .. } => ActionClass::FillField,
            Self::FormSelect { .. } => ActionClass::SelectOption,
            Self::FormToggle { .. } => ActionClass::ToggleControl,
            Self::FormSubmit { .. } => ActionClass::SubmitForm,
            Self::DownloadStart { .. }
            | Self::DownloadFromLink { .. }
            | Self::DownloadCancel { .. } => ActionClass::StartDownload,
        }
    }

    pub const fn idempotency(&self) -> IdempotencyClass {
        match self {
            Self::DomQuery { .. }
            | Self::DomRead { .. }
            | Self::TabsList { .. }
            | Self::FormInspect { .. }
            | Self::DownloadList { .. }
            | Self::SelectionRead { .. }
            | Self::ImageDescribe { .. }
            | Self::ImageReadText { .. }
            | Self::VideoInspect { .. }
            | Self::PdfInspect { .. }
            | Self::PageScreenshotInspect { .. } => IdempotencyClass::PureRead,
            Self::FormFill { .. }
            | Self::FormSelect { .. }
            | Self::FormToggle { .. }
            | Self::FormSubmit { .. }
            | Self::DownloadStart { .. }
            | Self::DownloadFromLink { .. } => IdempotencyClass::Consequential,
            Self::DownloadCancel { .. } => IdempotencyClass::IdempotentWrite,
            _ => IdempotencyClass::ConditionallyIdempotent,
        }
    }

    pub const fn tab_id(&self) -> &TabId {
        match self {
            Self::Navigate { tab, .. }
            | Self::Search { tab, .. }
            | Self::HistoryBack { tab }
            | Self::HistoryForward { tab }
            | Self::Reload { tab }
            | Self::StopLoading { tab }
            | Self::DomQuery { tab, .. }
            | Self::DomRead { tab, .. }
            | Self::DomScroll { tab, .. }
            | Self::FormInspect { tab, .. }
            | Self::FormFill { tab, .. }
            | Self::FormSelect { tab, .. }
            | Self::FormToggle { tab, .. }
            | Self::FormSubmit { tab, .. }
            | Self::DownloadStart { tab, .. }
            | Self::DownloadList { tab, .. }
            | Self::DownloadCancel { tab, .. }
            | Self::SelectionRead { tab }
            | Self::ImageDescribe { tab, .. }
            | Self::ImageReadText { tab, .. }
            | Self::VideoInspect { tab, .. }
            | Self::PdfInspect { tab }
            | Self::PageScreenshotInspect { tab } => tab,
            Self::TabsOpen { context, .. }
            | Self::TabsList { context, .. }
            | Self::TabsActivate { context, .. }
            | Self::TabsClose { context, .. } => context,
            Self::DomClick { target, .. }
            | Self::DomFocus { target }
            | Self::LinkOpen { target }
            | Self::DownloadFromLink { target, .. } => target.tab_id(),
        }
    }

    pub const fn node_id(&self) -> Option<&SemanticNodeId> {
        match self {
            Self::DomRead { target, .. } | Self::DomScroll { target, .. } => target.as_ref(),
            Self::ImageDescribe { target, .. }
            | Self::ImageReadText { target, .. }
            | Self::VideoInspect { target, .. } => Some(target),
            Self::DomClick { target, .. }
            | Self::DomFocus { target }
            | Self::LinkOpen { target }
            | Self::DownloadFromLink { target, .. } => Some(target.node_id()),
            Self::FormInspect { form, .. } => Some(form),
            Self::FormFill { field, .. }
            | Self::FormSelect { field, .. }
            | Self::FormToggle { field, .. } => Some(field),
            Self::FormSubmit { control, .. } => Some(control),
            // Includes DomQuery: its optional `within` is a Rust-side filter,
            // never the executable node of the document observation.
            _ => None,
        }
    }

    pub fn destination_address(&self) -> Option<&str> {
        match self {
            Self::Navigate { address, .. } | Self::DownloadStart { address, .. } => Some(address),
            Self::TabsOpen { address, .. } => address.as_deref(),
            _ => None,
        }
    }
}
