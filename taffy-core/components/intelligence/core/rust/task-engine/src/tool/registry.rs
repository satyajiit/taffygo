// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The complete compiled-in tool registry.
//!
//! Rows are stored in cohesive shards to keep each source reviewable. This
//! value is the only public registry and its iterator preserves appendix order.

mod browser;
mod other;
mod stores;

use super::ToolEntry;

const SHARDS: &[&[ToolEntry]] = &[browser::ROWS, other::ROWS, stores::ROWS];

/// A read-only view over every registry shard.
#[derive(Clone, Copy, Debug)]
pub struct Registry {
    shards: &'static [&'static [ToolEntry]],
}

/// Every registered name, in appendix order.
pub const REGISTRY: Registry = Registry { shards: SHARDS };

type RegistryIter =
    std::iter::Flatten<std::iter::Copied<std::slice::Iter<'static, &'static [ToolEntry]>>>;

impl Registry {
    pub fn iter(self) -> RegistryIter {
        self.shards.iter().copied().flatten()
    }

    pub fn is_empty(self) -> bool {
        self.shards.iter().all(|shard| shard.is_empty())
    }
}

impl IntoIterator for Registry {
    type Item = &'static ToolEntry;
    type IntoIter = RegistryIter;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl IntoIterator for &Registry {
    type Item = &'static ToolEntry;
    type IntoIter = RegistryIter;

    fn into_iter(self) -> Self::IntoIter {
        (*self).iter()
    }
}
