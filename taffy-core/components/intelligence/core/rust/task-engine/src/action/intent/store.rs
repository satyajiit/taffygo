// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact, bounded reads over a person's own stores handed to the task.
//!
//! History, Bookmarks and the person's open tabs reach a task as tools rather
//! than as prompt text (decision 0133). Every operation is a browser-executed
//! local read of class [`ActionClass::ProfileStoreRead`]; the query bytes stay
//! in the model-turn residency and the durable proposal carries only their
//! content-bound opaque reference.

use bip_types::identity::{SemanticNodeId, TabId};

use crate::authority::ActionClass;
use crate::tool::IdempotencyClass;

use super::OpaqueOperandRef;

/// Default number of store rows returned to one model call.
pub const DEFAULT_STORE_RESULTS: u32 = 10;
/// Contract ceiling for one store result set.
pub const MAX_STORE_RESULTS: u32 = 32;

/// Which of the person's stores an operation reads.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum StoreKind {
    History,
    Bookmarks,
    OpenTabs,
}

impl StoreKind {
    /// The allowlist group a start request names to attach this store.
    pub const fn allowlist_group(self) -> &'static str {
        match self {
            Self::History => "person.history",
            Self::Bookmarks => "person.bookmarks",
            Self::OpenTabs => "person.open_tabs",
        }
    }

    /// Every store, in wire order.
    pub const ALL: &'static [Self] = &[Self::History, Self::Bookmarks, Self::OpenTabs];
}

/// One typed read over an attached store.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StoreIntent {
    HistorySearch {
        tab: TabId,
        query: OpaqueOperandRef,
        limit: u32,
    },
    HistoryRecent {
        tab: TabId,
        limit: u32,
    },
    BookmarksSearch {
        tab: TabId,
        query: OpaqueOperandRef,
        limit: u32,
    },
    BookmarksList {
        tab: TabId,
        limit: u32,
    },
    OpenTabsList {
        tab: TabId,
    },
}

impl StoreIntent {
    pub const fn tool_name(&self) -> &'static str {
        match self {
            Self::HistorySearch { .. } => "history.search",
            Self::HistoryRecent { .. } => "history.recent",
            Self::BookmarksSearch { .. } => "bookmarks.search",
            Self::BookmarksList { .. } => "bookmarks.list",
            Self::OpenTabsList { .. } => "open_tabs.list",
        }
    }

    pub const fn store(&self) -> StoreKind {
        match self {
            Self::HistorySearch { .. } | Self::HistoryRecent { .. } => StoreKind::History,
            Self::BookmarksSearch { .. } | Self::BookmarksList { .. } => StoreKind::Bookmarks,
            Self::OpenTabsList { .. } => StoreKind::OpenTabs,
        }
    }

    pub const fn action_class(&self) -> ActionClass {
        ActionClass::ProfileStoreRead
    }

    pub const fn tab_id(&self) -> &TabId {
        match self {
            Self::HistorySearch { tab, .. }
            | Self::HistoryRecent { tab, .. }
            | Self::BookmarksSearch { tab, .. }
            | Self::BookmarksList { tab, .. }
            | Self::OpenTabsList { tab } => tab,
        }
    }

    pub const fn node_id(&self) -> Option<&SemanticNodeId> {
        None
    }

    pub const fn idempotency(&self) -> IdempotencyClass {
        IdempotencyClass::PureRead
    }

    /// The row cap this read asked for; the browser may answer fewer.
    pub const fn limit(&self) -> u32 {
        match self {
            Self::HistorySearch { limit, .. }
            | Self::HistoryRecent { limit, .. }
            | Self::BookmarksSearch { limit, .. }
            | Self::BookmarksList { limit, .. } => *limit,
            Self::OpenTabsList { .. } => MAX_STORE_RESULTS,
        }
    }
}
