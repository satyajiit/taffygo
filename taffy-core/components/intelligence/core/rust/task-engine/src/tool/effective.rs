// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one task may actually call: the registry, filtered twice.
//!
//! # Two filters, and only one of them has an exception
//!
//! A name reaches a task's effective set when the build's milestone has
//! reached it **and** the task's allowlist admits it. The milestone filter has
//! no exceptions at all — a tool the build does not have cannot be granted by
//! anything a task carries. The allowlist filter has exactly one, and
//! [`crate::tool::UNCONDITIONAL_TOOLS`] holds the argument for it: a narrowed
//! allowlist may remove a capability and may never remove the way out
//! (decision 0055 section 8).
//!
//! # Narrowing is a subset operation, not a merge
//!
//! [`EffectiveToolSet::narrow_by`] filters the set it is called on. It never
//! consults [`crate::tool::REGISTRY`] again, so there is no expression of "and
//! also allow" for a reviewer to miss. Decision 0055 section 3 makes this the
//! whole safety argument for letting a skill or a recorded procedure touch the
//! tool surface: a name a procedure could introduce would be an `ActionClass`
//! it could introduce, and an `ActionClass` is a permission.
//!
//! # A row is the smallest thing that can be admitted
//!
//! Open, list, activate and close are exact rows because their operands and
//! authority classes differ. They share the stable allowlist identity
//! `browser.tabs`; naming a member alone admits nothing, while naming the
//! reviewed group admits all four exact rows.
//!
//! # This set and the reducer's guard admit the same registry row
//!
//! A model proposes an exact callable name such as `page.images.describe`,
//! while an allowlist names the compiled-in namespace row `page.images`.
//! Exact tab rows similarly share the stable `browser.tabs` allowlist group.
//! Both this set and `Guard::ToolAvailable` resolve the callable name and
//! compare its row's stable allowlist identity. Naming only a member admits no
//! row and cannot silently grant sibling operations.
//!
//! The exact-vector test at `tests/effective_set_and_guard.rs` drives both
//! answers through a real reducer. This module's unit test states the same rule
//! beside the model-facing set so neither interface can drift alone.

use super::definition::ToolDefinition;
use super::entry::{ToolEntry, ToolLoading};
use super::milestone::Milestone;
use super::{allowlist_name, available_at, is_unconditional};
use crate::task::TaskTemplateId;

mod discovery;

/// The deferred rows an errand is shown on its first turn.
///
/// An errand exists to download a file or to get a form through, and a model
/// that has to discover `browser.download.start` by searching for it is one
/// that will narrate instead (decision 0136). Promotion is a loading change
/// only: every name here is still admitted by the milestone and the
/// allowlist first, and a row the set does not hold is not introduced.
///
/// `browser.form.select`, `.toggle` and `.submit` are deliberately absent.
/// They were here, and it was a no-op that read like a capability: all three
/// are withheld from every template allowlist, so promoting them advertised
/// three names no errand could ever call. `browser.form.fill` stays because
/// it is admitted.
const WEB_ERRAND_IMMEDIATE: &[&str] = &[
    "browser.tabs.list",
    "browser.tabs.activate",
    "browser.tabs.close",
    "browser.form.fill",
    "browser.download.start",
    "browser.download.from_link",
    "browser.download.list",
    "browser.download.cancel",
    "page.pdf.inspect",
];

/// The tools one task may call.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EffectiveToolSet {
    milestone: Milestone,
    entries: Vec<&'static ToolEntry>,
    loading: Vec<ToolLoading>,
}

impl EffectiveToolSet {
    /// The set a task at `milestone` with `allowlist` may call.
    ///
    /// An **empty** allowlist means "everything this milestone has", which is
    /// the reading the reducer's own guard has always used. It is worth saying
    /// out loud because the other reading is available and is wrong in the
    /// dangerous direction: a caller that meant "nothing" and expressed it as
    /// an empty list would get the whole milestone surface. Something that
    /// means nothing has to say so some other way.
    pub fn for_task(milestone: Milestone, allowlist: &[String]) -> Self {
        let admitted: Vec<&'static ToolEntry> = available_at(milestone)
            .into_iter()
            .filter(|entry| admitted_by(allowlist, entry.name))
            .collect();
        let loading = admitted.iter().map(|entry| entry.loading).collect();
        Self {
            milestone,
            entries: admitted,
            loading,
        }
    }

    /// The set a task of `template` may call, loaded the way that template
    /// needs it.
    ///
    /// The admitted rows are exactly [`Self::for_task`]'s; only which of them
    /// are offered before a `tool.activate` differs. A research template keeps
    /// every row's registered loading. A web errand is shown its download,
    /// form and tab rows immediately, because those are what it is for.
    pub fn for_template(
        template: TaskTemplateId,
        milestone: Milestone,
        allowlist: &[String],
    ) -> Self {
        let set = Self::for_task(milestone, allowlist);
        match template {
            TaskTemplateId::WebErrand => set.with_activated(WEB_ERRAND_IMMEDIATE),
            TaskTemplateId::CompareProducts
            | TaskTemplateId::SummarizeEvidence
            | TaskTemplateId::BuildSourceTable => set,
        }
    }

    /// The subset of this set that `allowlist` admits.
    ///
    /// A filter over what the set already holds. Nothing here can produce a
    /// name the set did not have, whatever the list says.
    #[must_use]
    pub fn narrow_by(&self, allowlist: &[String]) -> Self {
        let (entries, loading) = self
            .entries
            .iter()
            .copied()
            .zip(self.loading.iter().copied())
            .filter(|(entry, _)| admitted_by(allowlist, entry.name))
            .unzip();
        Self {
            milestone: self.milestone,
            entries,
            loading,
        }
    }

    /// Offers matching deferred rows that this set already holds.
    ///
    /// Activation is not a field of the set: the runtime holds the names and
    /// passes them here. A name the set does not already contain — later
    /// milestone, excluded, or off the allowlist — is ignored, so this cannot
    /// introduce a tool.
    #[must_use]
    pub fn with_activated(mut self, names: &[&str]) -> Self {
        for (entry, loading) in self.entries.iter().zip(self.loading.iter_mut()) {
            if *loading != ToolLoading::Deferred {
                continue;
            }
            if names.iter().any(|name| entry.callable_name(name).is_some()) {
                *loading = ToolLoading::Immediate;
            }
        }
        self
    }

    /// The milestone this set was built for.
    pub const fn milestone(&self) -> Milestone {
        self.milestone
    }

    /// The rows, in registration order. Deferred rows stay here; they are
    /// absent from [`Self::offered`] until activated.
    pub fn entries(&self) -> &[&'static ToolEntry] {
        &self.entries
    }

    /// Immediate rows this set admits, in registration order.
    pub fn immediate(&self) -> impl Iterator<Item = &'static ToolEntry> + '_ {
        self.entries
            .iter()
            .zip(self.loading.iter())
            .filter(|(_, loading)| **loading == ToolLoading::Immediate)
            .map(|(entry, _)| *entry)
    }

    /// Immediate rows this set admits, collected. This is the model-facing
    /// list: deferred names are findable through `tool.search` and join this
    /// list only after `tool.activate`.
    pub fn offered(&self) -> Vec<&'static ToolEntry> {
        self.immediate().collect()
    }

    /// Deferred rows this set admits, including those not yet activated.
    pub fn deferred(&self) -> Vec<&'static ToolEntry> {
        self.deferred_iter().collect()
    }

    fn deferred_iter(&self) -> impl Iterator<Item = &'static ToolEntry> + '_ {
        self.entries
            .iter()
            .zip(self.loading.iter())
            .filter(|(_, loading)| **loading == ToolLoading::Deferred)
            .map(|(entry, _)| *entry)
    }

    /// Deferred-available rows matching a bounded name or purpose query.
    ///
    /// Terms may occur in different parts of one callable definition. Exact
    /// names rank first, with registry order breaking ties. The search only
    /// receives this task's filtered rows, so it cannot reveal excluded tools.
    pub fn search(&self, query: &str) -> Vec<&'static ToolEntry> {
        discovery::rows(self.deferred_iter(), query)
    }

    /// Exact callable names matching `query`, ranked by the compiled definition.
    ///
    /// Namespace prefixes are never returned: they are reviewed allowlist
    /// identities, not names a model can call. Every value returned here is a
    /// `&'static str` from the compiled registry.
    pub fn search_names(&self, query: &str) -> Vec<&'static str> {
        discovery::names(self.deferred_iter(), query)
    }

    /// The registry-owned callable spelling of an admitted deferred name.
    ///
    /// Static loading is checked rather than the current loading view so an
    /// idempotent second activation of a name already loaded into this task
    /// still succeeds. Immediate tools cannot be activated, and a name absent
    /// from this already-filtered set cannot be introduced.
    pub fn activatable_name(&self, requested: &str) -> Option<&'static str> {
        self.entries.iter().find_map(|entry| {
            if entry.loading != ToolLoading::Deferred {
                return None;
            }
            entry.callable_name(requested)
        })
    }

    /// Whether `tool_name` may be called.
    ///
    /// Matched the way the registry matches, so a member of an admitted
    /// namespace is admitted and a name that merely shares a prefix is not.
    pub fn admits(&self, tool_name: &str) -> bool {
        self.entries.iter().any(|entry| entry.matches(tool_name))
    }

    /// Every admitted name, in registration order.
    pub fn names(&self) -> Vec<&'static str> {
        self.entries.iter().map(|entry| entry.name).collect()
    }

    /// What a model is shown, in registration order: the offered (immediate)
    /// rows only.
    pub fn definitions(&self) -> Vec<ToolDefinition> {
        self.immediate().flat_map(ToolEntry::definitions).collect()
    }

    /// How many admitted rows are in the set, including deferred ones.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the set is empty. A task here can do nothing at all, including
    /// stop — which is why it should not happen above milestone M3.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

fn admitted_by(allowlist: &[String], name: &str) -> bool {
    is_unconditional(name)
        || allowlist.is_empty()
        || allowlist
            .iter()
            .any(|allowed| allowed == allowlist_name(name))
}

#[cfg(test)]
mod tests;
