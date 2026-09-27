// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed records and the structured lookups over them.
//!
//! Every closed set in the schema is an enumeration here, and every
//! enumeration is closed: an unrecognized stored value is refused rather than
//! coerced to the nearest or least restrictive member. A sensitivity that
//! decoded to "probably fine" would be a privacy failure written as a
//! convenience.
//!
//! Reads honor deletion state. Once a record is marked for deletion it stops
//! being readable through the ordinary lookups, so nothing new is derived from
//! material the user has already asked to remove.
//!
//! # How this module is laid out
//!
//! | Module | Owns |
//! |---|---|
//! | [`vocabulary`] | Every closed set, and the rule that none of them decodes loosely |
//! | [`types`] | The five record types, as data |
//! | [`row`] | Reading one column of one row |
//! | [`workspace`], [`source`], [`observation`], [`fact`] | The lookups for one record type each |

mod fact;
mod observation;
mod row;
mod source;
mod types;
mod vocabulary;
mod workspace;

pub use self::fact::{fact_provenance, insert_fact, insert_provenance, live_facts, load_fact};
pub use self::observation::insert_observation;
pub use self::source::{attach_source, insert_source, load_source, readable_sources};
pub use self::types::{Fact, Observation, ProvenanceLocator, Source, Workspace};
pub use self::vocabulary::{
    DeletionState, FactClassification, FactStatus, MembershipState, Ownership, ProvenanceKind,
    Sensitivity, SourceKind, WorkspaceStatus,
};
pub use self::workspace::{insert_workspace, load_workspace};

/// The current record schema version written by this build.
pub const RECORD_SCHEMA_VERSION: i64 = 1;
