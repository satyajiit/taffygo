// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Aggregates this schema does not create yet, and why.
//!
//! An empty table is a claim that a feature exists. The domain model names
//! aggregates that belong to later milestones, and the honest thing is to leave
//! them out and say so here rather than ship a schema that looks finished.
//!
//! A test walks the migrated database and asserts that none of these tables
//! exists. When one of them arrives it arrives as a migration, a work package,
//! and a row removed from this list — not as a table somebody quietly added.
//!
//! Two have arrived that way. `provider_credential` and `catalog_cache` were
//! both listed here as M3 work; migration 9 creates them, and their rows are
//! gone from the list rather than edited to say "done". A deferral that stays
//! in the list after the table exists is worse than no list at all, because it
//! reads as a promise the schema has already broken.

/// One aggregate that is deliberately absent.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DeferredAggregate {
    /// The table name a future migration would use.
    pub table_name: &'static str,
    /// What it would hold.
    pub aggregate: &'static str,
    /// Which milestone owns it.
    pub milestone: &'static str,
    /// Why it is not here.
    pub reason: &'static str,
}

/// Everything the domain model names that this schema does not create.
pub const DEFERRED_AGGREGATES: &[DeferredAggregate] = &[
    DeferredAggregate {
        table_name: "embedding",
        aggregate: "Retrieval vectors",
        milestone: "M6",
        reason: "No vector engine is selected by anticipation; one is adopted only if the \
                 benchmark proves full text insufficient (OD-038)",
    },
    DeferredAggregate {
        table_name: "personality_profile",
        aggregate: "Personality presets",
        milestone: "M7",
        reason: "Versioned assistant preferences arrive with the presets milestone",
    },
];

/// Names this schema must never contain.
///
/// Chromium owns cookies, saved credentials, browsing history, downloads, and
/// site storage. This database stores opaque references to browser objects and
/// never a copy of the objects themselves, so a table or column named after one
/// is a design error rather than a naming quibble.
pub const CHROMIUM_OWNED_NAMES: &[&str] = &[
    "cookie",
    "password",
    "autofill",
    "browsing_history",
    "download",
    "site_storage",
    "site_setting",
    "session_restore",
];
