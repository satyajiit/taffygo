// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract browsing 1.1.

#![allow(
    clippy::module_name_repetitions,
    clippy::needless_question_mark,
    clippy::struct_excessive_bools,
    clippy::too_many_lines,
    clippy::trivially_copy_pass_by_ref
)]

pub const MAX_TABS: usize = 256;
pub const MAX_DOWNLOADS: usize = 256;
pub const MAX_IDENTIFIER_BYTES: usize = 256;
pub const MAX_HOST_BYTES: usize = 253;
pub const MAX_TITLE_BYTES: usize = 1_024;
pub const MAX_FILE_NAME_BYTES: usize = 512;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TabOwner {
    Unknown = 0,
    User = 1,
    Assistant = 2,
}

impl TabOwner {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Unknown),
            1 => Some(Self::User),
            2 => Some(Self::Assistant),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TabPrivacy {
    Normal = 0,
    Private = 1,
}

impl TabPrivacy {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Normal),
            1 => Some(Self::Private),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum NavigationFailure {
    None = 0,
    DnsFailure = 1,
    Offline = 2,
    ConnectionFailure = 3,
    Timeout = 4,
    TlsFailure = 5,
    HttpErrorStatus = 6,
    BlockedByClient = 7,
    BlockedBySafeBrowsing = 8,
    Aborted = 9,
    UnknownFailure = 10,
}

impl NavigationFailure {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::None),
            1 => Some(Self::DnsFailure),
            2 => Some(Self::Offline),
            3 => Some(Self::ConnectionFailure),
            4 => Some(Self::Timeout),
            5 => Some(Self::TlsFailure),
            6 => Some(Self::HttpErrorStatus),
            7 => Some(Self::BlockedByClient),
            8 => Some(Self::BlockedBySafeBrowsing),
            9 => Some(Self::Aborted),
            10 => Some(Self::UnknownFailure),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum InterstitialKind {
    None = 0,
    CertificateError = 1,
    SafeBrowsing = 2,
    BlockedByPolicy = 3,
    Other = 4,
}

impl InterstitialKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::None),
            1 => Some(Self::CertificateError),
            2 => Some(Self::SafeBrowsing),
            3 => Some(Self::BlockedByPolicy),
            4 => Some(Self::Other),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum DownloadState {
    Created = 0,
    InProgress = 1,
    Paused = 2,
    Interrupted = 3,
    Complete = 4,
    Cancelled = 5,
}

impl DownloadState {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Created),
            1 => Some(Self::InProgress),
            2 => Some(Self::Paused),
            3 => Some(Self::Interrupted),
            4 => Some(Self::Complete),
            5 => Some(Self::Cancelled),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum DownloadFailure {
    None = 0,
    Network = 1,
    Server = 2,
    FileSystem = 3,
    InsufficientSpace = 4,
    PermissionDenied = 5,
    BlockedBySecurityCheck = 6,
    CancelledByUser = 7,
    BrowserShutdown = 8,
    Unknown = 9,
}

impl DownloadFailure {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::None),
            1 => Some(Self::Network),
            2 => Some(Self::Server),
            3 => Some(Self::FileSystem),
            4 => Some(Self::InsufficientSpace),
            5 => Some(Self::PermissionDenied),
            6 => Some(Self::BlockedBySecurityCheck),
            7 => Some(Self::CancelledByUser),
            8 => Some(Self::BrowserShutdown),
            9 => Some(Self::Unknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum DownloadDestination {
    Undecided = 0,
    DefaultDirectory = 1,
    UserChosenLocation = 2,
    ApplicationPrivateDirectory = 3,
}

impl DownloadDestination {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Undecided),
            1 => Some(Self::DefaultDirectory),
            2 => Some(Self::UserChosenLocation),
            3 => Some(Self::ApplicationPrivateDirectory),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum DownloadCommand {
    Pause = 0,
    Resume = 1,
    Cancel = 2,
    Retry = 3,
    OpenWhenComplete = 4,
    OpenNow = 5,
}

impl DownloadCommand {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Pause),
            1 => Some(Self::Resume),
            2 => Some(Self::Cancel),
            3 => Some(Self::Retry),
            4 => Some(Self::OpenWhenComplete),
            5 => Some(Self::OpenNow),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BrowsingStatus {
    Accepted = 0,
    InvalidRequest = 1,
    UnknownTab = 2,
    UnknownDownload = 3,
    IllegalInState = 4,
    RefusedByPolicy = 5,
    NoHistory = 6,
    Unavailable = 7,
}

impl BrowsingStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Accepted),
            1 => Some(Self::InvalidRequest),
            2 => Some(Self::UnknownTab),
            3 => Some(Self::UnknownDownload),
            4 => Some(Self::IllegalInState),
            5 => Some(Self::RefusedByPolicy),
            6 => Some(Self::NoHistory),
            7 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TabView {
    pub tab_id: String,
    pub title: String,
    pub host: String,
    pub has_been_nowhere: bool,
    pub owner: TabOwner,
    pub privacy: TabPrivacy,
    pub selected: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NavigationView {
    pub host: String,
    pub title: String,
    pub can_go_back: bool,
    pub can_go_forward: bool,
    pub is_loading: bool,
    pub failure: NavigationFailure,
    pub interstitial: InterstitialKind,
    pub http_status_code: u32,
    pub filtering_active: bool,
    pub blocked_request_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DownloadView {
    pub download_id: String,
    pub file_name: String,
    pub host: String,
    pub received_bytes: u64,
    pub total_bytes: u64,
    pub total_known: bool,
    pub state: DownloadState,
    pub failure: DownloadFailure,
    pub destination: DownloadDestination,
    pub resumable: bool,
    pub requires_danger_confirmation: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BrowsingStateView {
    pub tabs: Vec<TabView>,
    pub navigation: NavigationView,
    pub downloads: Vec<DownloadView>,
}
