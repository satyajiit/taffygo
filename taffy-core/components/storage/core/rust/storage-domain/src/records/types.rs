// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The five record types, as values.
//!
//! Data only: no query, no executor, no deletion rule. Each of the lookup
//! modules beside this one owns the SQL for exactly one of these types, and
//! this module is what they agree on.

use super::vocabulary::{
    DeletionState, FactClassification, FactStatus, Ownership, ProvenanceKind, Sensitivity,
    SourceKind, WorkspaceStatus,
};
use crate::clock::Timestamp;
use crate::ids::{FactId, ObservationId, ProvenanceId, SourceId, WorkspaceId};
/// A workspace.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Workspace {
    /// Identity.
    pub workspace_id: WorkspaceId,
    /// The browser profile it belongs to. Exactly one, always.
    pub browser_profile_id: String,
    /// The account that owns it, when there is one.
    pub owner_account_id: Option<String>,
    /// What the user called it.
    pub title: String,
    /// Lifecycle state.
    pub status: WorkspaceStatus,
    /// Optimistic-concurrency revision.
    pub revision: i64,
    /// When it was created.
    pub created_at: Timestamp,
    /// When it last changed.
    pub updated_at: Timestamp,
    /// Which retention class governs it.
    pub retention_class: String,
    /// Its classification.
    pub sensitivity: Sensitivity,
    /// Whether deletion has started.
    pub deletion_state: DeletionState,
}

/// A source.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Source {
    /// Identity.
    pub source_id: SourceId,
    /// What kind of material it is.
    pub kind: SourceKind,
    /// The full locator, stored only where retention permits.
    pub canonical_locator: Option<String>,
    /// What a surface shows.
    pub display_locator: String,
    /// The origin, when it has one.
    pub origin: Option<String>,
    /// Its title, when it has one.
    pub title: Option<String>,
    /// When it was first seen.
    pub first_seen_at: Timestamp,
    /// When it was last observed.
    pub last_observed_at: Option<Timestamp>,
    /// Where it came from.
    pub ownership: Ownership,
    /// Its classification.
    pub sensitivity: Sensitivity,
    /// Which retention class governs it.
    pub retention_class: String,
    /// Whether deletion has started.
    pub deletion_state: DeletionState,
}

/// An immutable observation of a source.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Observation {
    /// Identity.
    pub observation_id: ObservationId,
    /// What was observed.
    pub source_id: SourceId,
    /// The task that observed it, when there was one.
    pub task_id: Option<String>,
    /// When.
    pub captured_at: Timestamp,
    /// How much of the source it covers.
    pub scope: String,
    /// What was left out.
    pub truncation: String,
    /// What was redacted, described rather than reproduced.
    pub redaction_summary: String,
    /// Where the evidence chain starts.
    pub provenance_root: String,
    /// Which retention class governs it.
    pub retention_class: String,
    /// Where the payload is, when one was kept.
    pub encrypted_payload_ref: Option<String>,
}

/// A normalized fact.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Fact {
    /// Identity.
    pub fact_id: FactId,
    /// The workspace it belongs to.
    pub workspace_id: WorkspaceId,
    /// What it is about.
    pub subject_key: String,
    /// What it says about the subject.
    pub predicate: String,
    /// The value, typed by the caller's own encoding.
    pub typed_value: String,
    /// Its unit, when it has one.
    pub unit: Option<String>,
    /// How it came to be, which stays visible forever.
    pub classification: FactClassification,
    /// Confidence in basis points, which is never authority.
    pub confidence_basis_points: Option<i64>,
    /// When the underlying observation happened.
    pub observation_time: Timestamp,
    /// Its classification.
    pub sensitivity: Sensitivity,
    /// Lifecycle state.
    pub status: FactStatus,
    /// The fact this one replaces, when it corrects one.
    pub supersedes_fact_id: Option<FactId>,
    /// Which retention class governs it.
    pub retention_class: String,
}

/// One piece of inspectable evidence for a fact.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProvenanceLocator {
    /// Identity.
    pub provenance_id: ProvenanceId,
    /// The fact it supports.
    pub fact_id: FactId,
    /// The source it cites.
    pub source_id: SourceId,
    /// The observation it cites, when there is one.
    pub observation_id: Option<ObservationId>,
    /// What kind of evidence it is.
    pub kind: ProvenanceKind,
    /// Where in the source, described for a human.
    pub location_descriptor: Option<String>,
    /// Which extraction rule produced it.
    pub extraction_rule_version: Option<String>,
    /// What happened to the value on the way.
    pub transformation_chain: String,
    /// When it was captured.
    pub captured_at: Timestamp,
}
