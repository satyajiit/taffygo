// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::super::BrowserIntent;

pub(super) const fn tag(intent: &BrowserIntent) -> u8 {
    match intent {
        BrowserIntent::Navigate { .. } => 0,
        BrowserIntent::Search { .. } => 1,
        BrowserIntent::HistoryBack { .. } => 2,
        BrowserIntent::HistoryForward { .. } => 3,
        BrowserIntent::TabsOpen { .. } => 4,
        BrowserIntent::TabsList { .. } => 5,
        BrowserIntent::TabsActivate { .. } => 6,
        BrowserIntent::TabsClose { .. } => 7,
        BrowserIntent::DomQuery { .. } => 8,
        BrowserIntent::DomRead { .. } => 9,
        BrowserIntent::DomClick { .. } => 10,
        BrowserIntent::DomScroll { .. } => 11,
        BrowserIntent::FormInspect { .. } => 12,
        BrowserIntent::FormFill { .. } => 13,
        BrowserIntent::FormSubmit { .. } => 14,
        BrowserIntent::DownloadStart { .. } => 15,
        BrowserIntent::DownloadList { .. } => 16,
        BrowserIntent::SelectionRead { .. } => 17,
        BrowserIntent::ImageDescribe { .. } => 18,
        BrowserIntent::ImageReadText { .. } => 19,
        BrowserIntent::VideoInspect { .. } => 20,
        BrowserIntent::PdfInspect { .. } => 21,
        BrowserIntent::LinkOpen { .. } => 22,
        BrowserIntent::FormSelect { .. } => 23,
        BrowserIntent::FormToggle { .. } => 24,
        BrowserIntent::DomFocus { .. } => 25,
        BrowserIntent::PageScreenshotInspect { .. } => 26,
        BrowserIntent::DownloadCancel { .. } => 27,
        BrowserIntent::Reload { .. } => 28,
        BrowserIntent::StopLoading { .. } => 29,
        BrowserIntent::DownloadFromLink { .. } => 30,
    }
}
