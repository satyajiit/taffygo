// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The sources a task is allowed to work from.
//!
//! One responsibility: which sources are in scope, which the user removed, and
//! the rule that removal is durable — a source the user excluded does not come
//! back because a later observation mentions it.

use crate::records::SourceId;
/// The sources a task is allowed to work from.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SourceScope {
    /// The sources in scope, in a deterministic order.
    included: Vec<SourceId>,
    /// The sources the user removed from scope.
    excluded: Vec<SourceId>,
}

impl SourceScope {
    /// An empty scope.
    pub const fn new() -> Self {
        Self {
            included: Vec::new(),
            excluded: Vec::new(),
        }
    }

    /// The same scope with `source` included, if it is not excluded already.
    #[must_use]
    pub fn include(mut self, source: SourceId) -> Self {
        if !self.excluded.contains(&source) && !self.included.contains(&source) {
            self.included.push(source);
            self.included.sort_unstable();
        }
        self
    }

    /// Removes a source from scope and records the exclusion.
    ///
    /// Excluding is durable: a source the user removed does not come back
    /// because a later observation mentions it.
    pub fn exclude(&mut self, source: &SourceId) -> bool {
        let was_included = self.included.iter().any(|held| held == source);
        self.included.retain(|held| held != source);
        if !self.excluded.contains(source) {
            self.excluded.push(*source);
            self.excluded.sort_unstable();
        }
        was_included
    }

    /// The sources in scope.
    pub fn included(&self) -> &[SourceId] {
        &self.included
    }

    /// The sources the user removed.
    pub fn excluded(&self) -> &[SourceId] {
        &self.excluded
    }

    /// How many sources are in scope.
    pub fn len(&self) -> usize {
        self.included.len()
    }

    /// Whether the scope is empty.
    pub fn is_empty(&self) -> bool {
        self.included.is_empty()
    }
}
