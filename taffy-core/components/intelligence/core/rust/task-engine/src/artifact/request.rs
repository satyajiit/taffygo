// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What an artifact is generated from.
//!
//! Input only. Every field is a stored record or a value the runtime wrote
//! from the user's own goal — no model text enters an artifact through this
//! type, and a fact arrives with its evidence attached rather than with a
//! promise that evidence exists somewhere.

use crate::ids::ArtifactId;
use crate::records::{Fact, ProvenanceLocator, Source};
/// A fact together with the evidence for it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CitedFact {
    /// The stored fact.
    pub fact: Fact,
    /// Its provenance locators.
    pub provenance: Vec<ProvenanceLocator>,
}

/// What an artifact is generated from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ArtifactRequest {
    /// The identifier the artifact will be recorded under.
    pub artifact_id: ArtifactId,
    /// The title, written by the runtime from the user's own goal.
    pub title: String,
    /// The output schema version this build writes.
    pub schema_version: u32,
    /// Every source the facts cite.
    pub sources: Vec<Source>,
    /// The facts, with their evidence.
    pub facts: Vec<CitedFact>,
}
