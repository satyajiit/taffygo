// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed operation identity retained beside the broader policy action class.
//!
//! A class answers how consequential an action is. An operation answers what
//! exact compiled-in browser or tool primitive the grant covers. They are
//! deliberately both bound into a grant so two operations in the same class
//! cannot spend each other's authority.

use crate::action_class::ActionClass;

/// Exact compiled-in operation a policy request and minted grant bind.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum ActionOperationKind {
    Navigate,
    Search,
    HistoryBack,
    HistoryForward,
    TabsOpen,
    TabsList,
    TabsActivate,
    TabsClose,
    DomQuery,
    DomRead,
    DomClick,
    DomFocus,
    DomScroll,
    FormInspect,
    FormFill,
    FormSelect,
    FormToggle,
    FormSubmit,
    DownloadStart,
    DownloadList,
    DownloadCancel,
    SelectionRead,
    ImageDescribe,
    ImageReadText,
    VideoInspect,
    PdfInspect,
    PageScreenshotInspect,
    LinkOpen,
    ToolJob,
    LibrarySearch,
    LibrarySave,
    LibraryRemove,
    MemorySearch,
    MemorySave,
    MemoryUpdate,
    MemoryDelete,
    Reload,
    StopLoading,
    HistorySearch,
    HistoryRecent,
    BookmarksSearch,
    BookmarksList,
    OpenTabsList,
}

impl ActionOperationKind {
    /// Consequence class derived from this exact compiled-in operation.
    pub const fn action_class(self) -> ActionClass {
        match self {
            Self::Navigate | Self::Search | Self::LinkOpen => ActionClass::OpenLink,
            Self::HistoryBack | Self::HistoryForward | Self::Reload | Self::StopLoading => {
                ActionClass::ControlTab
            }
            Self::TabsOpen | Self::TabsClose => ActionClass::CreateTaskTab,
            Self::TabsActivate | Self::DomFocus => ActionClass::MoveFocus,
            Self::DomClick => ActionClass::SyntheticClick,
            Self::DomScroll => ActionClass::ScrollIntoView,
            Self::FormFill => ActionClass::FillField,
            Self::FormSelect => ActionClass::SelectOption,
            Self::FormToggle => ActionClass::ToggleControl,
            Self::FormSubmit => ActionClass::SubmitForm,
            Self::DownloadStart | Self::DownloadCancel => ActionClass::StartDownload,
            Self::ToolJob => ActionClass::ExecuteToolJob,
            Self::LibrarySearch => ActionClass::LibraryRead,
            Self::LibrarySave | Self::LibraryRemove => ActionClass::LibraryWrite,
            Self::MemorySearch => ActionClass::MemoryRead,
            Self::MemorySave | Self::MemoryUpdate | Self::MemoryDelete => ActionClass::MemoryWrite,
            Self::HistorySearch
            | Self::HistoryRecent
            | Self::BookmarksSearch
            | Self::BookmarksList
            | Self::OpenTabsList => ActionClass::ProfileStoreRead,
            Self::TabsList
            | Self::DomQuery
            | Self::DomRead
            | Self::FormInspect
            | Self::DownloadList
            | Self::SelectionRead
            | Self::ImageDescribe
            | Self::ImageReadText
            | Self::VideoInspect
            | Self::PdfInspect
            | Self::PageScreenshotInspect => ActionClass::ObservePage,
        }
    }

    /// Whether this operation can carry an exact destination address.
    pub const fn admits_destination_address(self) -> bool {
        matches!(
            self,
            Self::Navigate | Self::Search | Self::TabsOpen | Self::DownloadStart | Self::LinkOpen
        )
    }

    /// Whether omitting an exact destination would change this operation.
    pub const fn requires_destination_address(self) -> bool {
        matches!(
            self,
            Self::Navigate | Self::Search | Self::TabsOpen | Self::DownloadStart | Self::LinkOpen
        )
    }

    /// Whether the operation's semantic-node scope has its exact shape.
    ///
    /// This is deliberately total over the closed operation vocabulary. A
    /// broad action class cannot answer this question: `FormInspect` and
    /// `SelectionRead` are both observations, but only the former names a
    /// node. Keeping the table on the operation makes grant minting and wire
    /// projection share one fail-closed answer.
    pub const fn node_scope_is_valid(self, has_node: bool) -> bool {
        match self {
            // Queries may name an exact root. Downloads can instead name the
            // exact observed link whose destination the browser resolves;
            // canonical intent and tool-name checks distinguish that shape
            // from an address-based start before policy is asked.
            Self::DomQuery | Self::DownloadStart => true,
            Self::DomClick
            | Self::DomFocus
            | Self::DomScroll
            | Self::FormInspect
            | Self::FormFill
            | Self::FormSelect
            | Self::FormToggle
            | Self::FormSubmit
            | Self::ImageDescribe
            | Self::ImageReadText
            | Self::VideoInspect
            | Self::LinkOpen => has_node,
            Self::Navigate
            | Self::Search
            | Self::HistoryBack
            | Self::HistoryForward
            | Self::Reload
            | Self::StopLoading
            | Self::TabsOpen
            | Self::TabsList
            | Self::TabsActivate
            | Self::TabsClose
            | Self::DomRead
            | Self::DownloadList
            | Self::DownloadCancel
            | Self::SelectionRead
            | Self::PdfInspect
            | Self::PageScreenshotInspect
            | Self::ToolJob
            | Self::LibrarySearch
            | Self::LibrarySave
            | Self::LibraryRemove
            | Self::MemorySearch
            | Self::MemorySave
            | Self::MemoryUpdate
            | Self::MemoryDelete
            | Self::HistorySearch
            | Self::HistoryRecent
            | Self::BookmarksSearch
            | Self::BookmarksList
            | Self::OpenTabsList => !has_node,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::ActionOperationKind;

    #[test]
    fn every_operation_has_one_exact_node_shape() {
        for operation in [
            ActionOperationKind::DomClick,
            ActionOperationKind::DomFocus,
            ActionOperationKind::DomScroll,
            ActionOperationKind::FormInspect,
            ActionOperationKind::FormFill,
            ActionOperationKind::FormSelect,
            ActionOperationKind::FormToggle,
            ActionOperationKind::FormSubmit,
            ActionOperationKind::ImageDescribe,
            ActionOperationKind::ImageReadText,
            ActionOperationKind::VideoInspect,
            ActionOperationKind::LinkOpen,
        ] {
            assert!(operation.node_scope_is_valid(true), "{operation:?}");
            assert!(!operation.node_scope_is_valid(false), "{operation:?}");
        }
        for operation in [
            ActionOperationKind::Navigate,
            ActionOperationKind::Search,
            ActionOperationKind::HistoryBack,
            ActionOperationKind::HistoryForward,
            ActionOperationKind::Reload,
            ActionOperationKind::StopLoading,
            ActionOperationKind::TabsOpen,
            ActionOperationKind::TabsList,
            ActionOperationKind::TabsActivate,
            ActionOperationKind::TabsClose,
            ActionOperationKind::DomRead,
            ActionOperationKind::DownloadList,
            ActionOperationKind::DownloadCancel,
            ActionOperationKind::SelectionRead,
            ActionOperationKind::PdfInspect,
            ActionOperationKind::PageScreenshotInspect,
            ActionOperationKind::ToolJob,
            ActionOperationKind::HistorySearch,
            ActionOperationKind::HistoryRecent,
            ActionOperationKind::BookmarksSearch,
            ActionOperationKind::BookmarksList,
            ActionOperationKind::OpenTabsList,
        ] {
            assert!(operation.node_scope_is_valid(false), "{operation:?}");
            assert!(!operation.node_scope_is_valid(true), "{operation:?}");
        }
        for operation in [
            ActionOperationKind::DomQuery,
            ActionOperationKind::DownloadStart,
        ] {
            assert!(operation.node_scope_is_valid(false));
            assert!(operation.node_scope_is_valid(true));
        }
    }
}
