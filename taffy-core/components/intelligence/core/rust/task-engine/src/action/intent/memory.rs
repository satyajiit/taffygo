// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact, content-bounded operations over profile-local Memory.

use bip_types::identity::{SemanticNodeId, TabId};

use crate::authority::ActionClass;
use crate::tool::IdempotencyClass;

use super::OpaqueOperandRef;

/// Default number of Memory results returned to one model call.
pub const DEFAULT_MEMORY_SEARCH_RESULTS: u32 = 10;
/// Contract ceiling for one Memory result set.
pub const MAX_MEMORY_SEARCH_RESULTS: u32 = 32;

/// Visible future-task scope proposed for a Memory statement.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum MemoryScopeIntent {
    AllTasks,
    Workspace { workspace_id: String },
}

/// One typed Memory operation.
///
/// Query and statement bytes remain in the exact model-turn residency. The
/// durable proposal carries only their content-bound opaque references.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum MemoryIntent {
    Search {
        tab: TabId,
        query: OpaqueOperandRef,
        limit: u32,
    },
    Save {
        tab: TabId,
        statement: OpaqueOperandRef,
        scope: MemoryScopeIntent,
        expires_at_epoch_ms: Option<u64>,
    },
    Update {
        tab: TabId,
        memory_id: String,
        record_revision: u64,
        statement: OpaqueOperandRef,
        scope: MemoryScopeIntent,
        expires_at_epoch_ms: Option<u64>,
    },
    Delete {
        tab: TabId,
        memory_id: String,
        record_revision: u64,
    },
}

impl MemoryIntent {
    pub const fn tool_name(&self) -> &'static str {
        match self {
            Self::Search { .. } => "memory.search",
            Self::Save { .. } => "memory.save",
            Self::Update { .. } => "memory.update",
            Self::Delete { .. } => "memory.delete",
        }
    }

    pub const fn action_class(&self) -> ActionClass {
        match self {
            Self::Search { .. } => ActionClass::MemoryRead,
            Self::Save { .. } | Self::Update { .. } | Self::Delete { .. } => {
                ActionClass::MemoryWrite
            }
        }
    }

    pub const fn tab_id(&self) -> &TabId {
        match self {
            Self::Search { tab, .. }
            | Self::Save { tab, .. }
            | Self::Update { tab, .. }
            | Self::Delete { tab, .. } => tab,
        }
    }

    pub const fn node_id(&self) -> Option<&SemanticNodeId> {
        None
    }

    pub const fn idempotency(&self) -> IdempotencyClass {
        match self {
            Self::Search { .. } => IdempotencyClass::PureRead,
            Self::Save { .. } | Self::Update { .. } | Self::Delete { .. } => {
                IdempotencyClass::IdempotentWrite
            }
        }
    }
}
