// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generation-resident handles for browser-verified task downloads.
//!
//! Chromium owns every real GUID and all page-authored metadata. The model sees
//! only short numbers and closed lifecycle words. A browser/core restart drops
//! this table, so a number can never be rebound to another manager incarnation.

use std::collections::BTreeSet;

use crate::BrowserSessionId;

/// Most exact-tab downloads one result may expose to a model.
pub const MAX_TASK_DOWNLOAD_RESULTS: usize = 16;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskDownloadState {
    Created,
    InProgress,
    Paused,
    Complete,
    Interrupted,
    Cancelled,
}

impl TaskDownloadState {
    pub(super) const fn word(self) -> &'static str {
        match self {
            Self::Created => "created",
            Self::InProgress => "in progress",
            Self::Paused => "paused",
            Self::Complete => "complete",
            Self::Interrupted => "interrupted",
            Self::Cancelled => "cancelled",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskDownloadMediaType {
    Unknown,
    Application,
    Audio,
    Font,
    Image,
    Message,
    Model,
    Multipart,
    Text,
    Video,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskDownloadDirectoryClass {
    Undecided,
    PersonChosen,
    DefaultDownloads,
    ApplicationPrivate,
}

/// One content-free manager witness. The opaque GUID is never rendered.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskDownloadSnapshot {
    download_id: String,
    state: TaskDownloadState,
    media_type: TaskDownloadMediaType,
    received_bytes: u64,
    directory_class: TaskDownloadDirectoryClass,
}

impl TaskDownloadSnapshot {
    pub fn new(
        download_id: String,
        state: TaskDownloadState,
        media_type: TaskDownloadMediaType,
        received_bytes: u64,
        directory_class: TaskDownloadDirectoryClass,
    ) -> Result<Self, TaskDownloadResultError> {
        if download_id.is_empty()
            || download_id.len() > 256
            || download_id
                .bytes()
                .any(|value| value < 0x20 || value == 0x7f)
        {
            return Err(TaskDownloadResultError::InvalidDownloadId);
        }
        Ok(Self {
            download_id,
            state,
            media_type,
            received_bytes,
            directory_class,
        })
    }

    pub fn download_id(&self) -> &str {
        &self.download_id
    }

    pub const fn state(&self) -> TaskDownloadState {
        self.state
    }

    pub const fn media_type(&self) -> TaskDownloadMediaType {
        self.media_type
    }

    pub const fn received_bytes(&self) -> u64 {
        self.received_bytes
    }

    pub const fn directory_class(&self) -> TaskDownloadDirectoryClass {
        self.directory_class
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TaskDownloadActionResult {
    Started {
        browser_session_id: BrowserSessionId,
        download: TaskDownloadSnapshot,
    },
    Listed {
        browser_session_id: BrowserSessionId,
        downloads: Vec<TaskDownloadSnapshot>,
        truncated: bool,
    },
    Cancelled {
        browser_session_id: BrowserSessionId,
        download: TaskDownloadSnapshot,
    },
}

impl TaskDownloadActionResult {
    pub fn started(browser_session_id: BrowserSessionId, download: TaskDownloadSnapshot) -> Self {
        Self::Started {
            browser_session_id,
            download,
        }
    }

    pub fn listed(
        browser_session_id: BrowserSessionId,
        downloads: Vec<TaskDownloadSnapshot>,
        truncated: bool,
    ) -> Result<Self, TaskDownloadResultError> {
        Self::validate_list(&downloads, truncated)?;
        Ok(Self::Listed {
            browser_session_id,
            downloads,
            truncated,
        })
    }

    pub fn cancelled(
        browser_session_id: BrowserSessionId,
        download: TaskDownloadSnapshot,
    ) -> Result<Self, TaskDownloadResultError> {
        if download.state != TaskDownloadState::Cancelled {
            return Err(TaskDownloadResultError::InvalidCancellation);
        }
        Ok(Self::Cancelled {
            browser_session_id,
            download,
        })
    }

    pub(super) fn validate_list(
        downloads: &[TaskDownloadSnapshot],
        truncated: bool,
    ) -> Result<(), TaskDownloadResultError> {
        if downloads.len() > MAX_TASK_DOWNLOAD_RESULTS {
            return Err(TaskDownloadResultError::TooManyDownloads);
        }
        if truncated && downloads.len() != MAX_TASK_DOWNLOAD_RESULTS {
            return Err(TaskDownloadResultError::InvalidTruncation);
        }
        let mut ids = BTreeSet::new();
        if downloads
            .iter()
            .any(|download| !ids.insert(download.download_id.as_str()))
        {
            return Err(TaskDownloadResultError::DuplicateDownload);
        }
        Ok(())
    }

    pub const fn browser_session_id(&self) -> &BrowserSessionId {
        match self {
            Self::Started {
                browser_session_id, ..
            }
            | Self::Listed {
                browser_session_id, ..
            }
            | Self::Cancelled {
                browser_session_id, ..
            } => browser_session_id,
        }
    }

    pub const fn tool_name(&self) -> &'static str {
        match self {
            Self::Started { .. } => "browser.download.start",
            Self::Listed { .. } => "browser.download.list",
            Self::Cancelled { .. } => "browser.download.cancel",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskDownloadResultError {
    InvalidDownloadId,
    TooManyDownloads,
    DuplicateDownload,
    InvalidTruncation,
    WrongBrowserSession,
    InvalidCancellation,
}

mod handles;
mod transcript;

pub use self::handles::TaskDownloadHandleTable;
pub use self::transcript::{TaskDownloadTranscriptEntry, TaskDownloadTranscriptOutcome};

#[cfg(test)]
mod tests;
