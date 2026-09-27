// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact, content-bounded operations over the profile-local Library.

use bip_types::identity::{SemanticNodeId, TabId};

use crate::authority::ActionClass;
use crate::tool::IdempotencyClass;

use super::OpaqueOperandRef;

/// Default number of Library results returned to one model call.
pub const DEFAULT_LIBRARY_SEARCH_RESULTS: u32 = 10;
/// Contract ceiling for one Library result set.
pub const MAX_LIBRARY_SEARCH_RESULTS: u32 = 32;

/// One typed Library operation. Mutable targets carry the exact revision the
/// model observed; the profile-global revision is bound by the runtime at
/// execution and checked atomically by browser storage.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LibraryIntent {
    Search {
        tab: TabId,
        query: OpaqueOperandRef,
        limit: u32,
    },
    Save {
        tab: TabId,
        workspace_id: String,
        workspace_revision: u64,
        fact_id: String,
        entry_revision: u64,
    },
    Remove {
        tab: TabId,
        entry_id: String,
        entry_revision: u64,
    },
}

impl LibraryIntent {
    pub const fn tool_name(&self) -> &'static str {
        match self {
            Self::Search { .. } => "library.search",
            Self::Save { .. } => "library.save",
            Self::Remove { .. } => "library.remove",
        }
    }

    pub const fn action_class(&self) -> ActionClass {
        match self {
            Self::Search { .. } => ActionClass::LibraryRead,
            Self::Save { .. } | Self::Remove { .. } => ActionClass::LibraryWrite,
        }
    }

    pub const fn tab_id(&self) -> &TabId {
        match self {
            Self::Search { tab, .. } | Self::Save { tab, .. } | Self::Remove { tab, .. } => tab,
        }
    }

    pub const fn node_id(&self) -> Option<&SemanticNodeId> {
        None
    }

    pub const fn idempotency(&self) -> IdempotencyClass {
        match self {
            Self::Search { .. } => IdempotencyClass::PureRead,
            Self::Save { .. } | Self::Remove { .. } => IdempotencyClass::IdempotentWrite,
        }
    }
}
