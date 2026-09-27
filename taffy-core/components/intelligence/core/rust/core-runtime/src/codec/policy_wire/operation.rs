// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed operation identity conversion for policy requests.

use core_service_types as wire;
use policy_engine::ActionOperationKind;

pub(super) const fn operation_kind(value: wire::TaskActionOperationKind) -> ActionOperationKind {
    match value {
        wire::TaskActionOperationKind::Navigate => ActionOperationKind::Navigate,
        wire::TaskActionOperationKind::Search => ActionOperationKind::Search,
        wire::TaskActionOperationKind::HistoryBack => ActionOperationKind::HistoryBack,
        wire::TaskActionOperationKind::HistoryForward => ActionOperationKind::HistoryForward,
        wire::TaskActionOperationKind::TabsOpen => ActionOperationKind::TabsOpen,
        wire::TaskActionOperationKind::TabsList => ActionOperationKind::TabsList,
        wire::TaskActionOperationKind::TabsActivate => ActionOperationKind::TabsActivate,
        wire::TaskActionOperationKind::TabsClose => ActionOperationKind::TabsClose,
        wire::TaskActionOperationKind::DomQuery => ActionOperationKind::DomQuery,
        wire::TaskActionOperationKind::DomRead => ActionOperationKind::DomRead,
        wire::TaskActionOperationKind::DomClick => ActionOperationKind::DomClick,
        wire::TaskActionOperationKind::DomFocus => ActionOperationKind::DomFocus,
        wire::TaskActionOperationKind::DomScroll => ActionOperationKind::DomScroll,
        wire::TaskActionOperationKind::FormInspect => ActionOperationKind::FormInspect,
        wire::TaskActionOperationKind::FormFill => ActionOperationKind::FormFill,
        wire::TaskActionOperationKind::FormSelect => ActionOperationKind::FormSelect,
        wire::TaskActionOperationKind::FormToggle => ActionOperationKind::FormToggle,
        wire::TaskActionOperationKind::FormSubmit => ActionOperationKind::FormSubmit,
        wire::TaskActionOperationKind::DownloadStart => ActionOperationKind::DownloadStart,
        wire::TaskActionOperationKind::DownloadList => ActionOperationKind::DownloadList,
        wire::TaskActionOperationKind::DownloadCancel => ActionOperationKind::DownloadCancel,
        wire::TaskActionOperationKind::SelectionRead => ActionOperationKind::SelectionRead,
        wire::TaskActionOperationKind::ImageDescribe => ActionOperationKind::ImageDescribe,
        wire::TaskActionOperationKind::ImageReadText => ActionOperationKind::ImageReadText,
        wire::TaskActionOperationKind::VideoInspect => ActionOperationKind::VideoInspect,
        wire::TaskActionOperationKind::PdfInspect => ActionOperationKind::PdfInspect,
        wire::TaskActionOperationKind::PageScreenshotInspect => {
            ActionOperationKind::PageScreenshotInspect
        }
        wire::TaskActionOperationKind::LinkOpen => ActionOperationKind::LinkOpen,
        wire::TaskActionOperationKind::ToolJob => ActionOperationKind::ToolJob,
        wire::TaskActionOperationKind::LibrarySearch => ActionOperationKind::LibrarySearch,
        wire::TaskActionOperationKind::LibrarySave => ActionOperationKind::LibrarySave,
        wire::TaskActionOperationKind::LibraryRemove => ActionOperationKind::LibraryRemove,
        wire::TaskActionOperationKind::MemorySearch => ActionOperationKind::MemorySearch,
        wire::TaskActionOperationKind::MemorySave => ActionOperationKind::MemorySave,
        wire::TaskActionOperationKind::MemoryUpdate => ActionOperationKind::MemoryUpdate,
        wire::TaskActionOperationKind::MemoryDelete => ActionOperationKind::MemoryDelete,
        wire::TaskActionOperationKind::Reload => ActionOperationKind::Reload,
        wire::TaskActionOperationKind::StopLoading => ActionOperationKind::StopLoading,
        wire::TaskActionOperationKind::HistorySearch => ActionOperationKind::HistorySearch,
        wire::TaskActionOperationKind::HistoryRecent => ActionOperationKind::HistoryRecent,
        wire::TaskActionOperationKind::BookmarksSearch => ActionOperationKind::BookmarksSearch,
        wire::TaskActionOperationKind::BookmarksList => ActionOperationKind::BookmarksList,
        wire::TaskActionOperationKind::OpenTabsList => ActionOperationKind::OpenTabsList,
    }
}
