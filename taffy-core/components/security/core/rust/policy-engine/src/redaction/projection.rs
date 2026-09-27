// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one node looks like at one destination.
//!
//! Four record types, one per destination, and the differences between them are
//! the point. The local observation keeps the page's own text because the task
//! isolated core needs it to plan; the model projection is selected, minimized, and
//! masked; the audit record carries identifiers, decision facts, and a
//! normalized origin; and the telemetry record is content-free **by
//! construction** — every field it holds is an enumerated name, a number, or a
//! boolean, so there is nothing for a careless caller to fill in.

use bip_types::identity::SemanticNodeId;
use bip_types::sensitivity::SensitivitySet;
use bip_types::snapshot::{NodeState, SemanticRole};

use crate::redaction::destination::RedactionDestination;

/// The rich local observation the isolated task engine holds.
///
/// It stays inside the process. Every value in it has already had prohibited
/// values removed by the renderer adapter, but it is otherwise unmasked: the
/// isolated core needs the real text to plan, and nothing leaves it without passing
/// through this module again for a narrower destination.
#[derive(Clone, Debug, PartialEq)]
pub struct LocalProjection {
    /// The node this is about.
    pub node_id: SemanticNodeId,
    /// Its role.
    pub role: SemanticRole,
    /// The accessible name.
    pub name: Option<String>,
    /// The description.
    pub description: Option<String>,
    /// Text runs.
    pub text_runs: Vec<String>,
    /// The current value, when the classification allows it anywhere.
    pub value: Option<String>,
    /// The destination, in full.
    pub destination_url: Option<String>,
    /// Asserted states.
    pub states: Vec<NodeState>,
    /// The classification every signal joined to.
    pub sensitivity: SensitivitySet,
    /// The structural placeholder, for a never-extract field.
    pub value_placeholder: Option<&'static str>,
}

/// The projection a model provider receives.
///
/// Selected, minimized, and masked. The full URL is absent by construction, and
/// a value is carried only when the classification allows it at a remote
/// destination.
#[derive(Clone, Debug, PartialEq)]
pub struct ModelProjection {
    /// The role.
    pub role: SemanticRole,
    /// The masked accessible name.
    pub name: Option<String>,
    /// The masked description.
    pub description: Option<String>,
    /// Masked text runs.
    pub text_runs: Vec<String>,
    /// The masked value, when one may be carried at all.
    pub value: Option<String>,
    /// The destination as origin and path, never the query or the fragment.
    pub destination: Option<String>,
    /// Asserted states.
    pub states: Vec<NodeState>,
    /// The classification every signal joined to.
    pub sensitivity: SensitivitySet,
    /// The structural placeholder, for a never-extract field.
    pub value_placeholder: Option<&'static str>,
    /// How many spans the masker replaced.
    pub masked_spans: u64,
    /// How many content fields the destination policy withheld.
    pub withheld_fields: u64,
}

/// The durable local audit record for one node.
///
/// Identifiers, decision facts, and a normalized origin. No page text, no
/// values, and no paths.
#[derive(Clone, Debug, PartialEq)]
pub struct AuditProjection {
    /// The node this is about. An identifier is not page content.
    pub node_id: SemanticNodeId,
    /// The role.
    pub role: SemanticRole,
    /// Asserted states.
    pub states: Vec<NodeState>,
    /// The classification every signal joined to.
    pub sensitivity: SensitivitySet,
    /// The normalized destination origin.
    pub destination_origin: Option<String>,
    /// Whether the node had a name, without saying what it was.
    pub name_present: bool,
    /// Whether the node held a value, without saying what it was.
    pub value_present: bool,
    /// How many content fields the destination policy withheld.
    pub withheld_fields: u64,
}

/// The operational telemetry projection.
///
/// Content-free by construction: there is no field here that can hold a string
/// taken from a page, so no policy has to be trusted to keep one out.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TelemetryProjection {
    /// The role.
    pub role: SemanticRole,
    /// The classification every signal joined to.
    pub sensitivity: SensitivitySet,
    /// How many states were asserted.
    pub state_count: u64,
    /// How many text runs the node had.
    pub text_run_count: u64,
    /// Whether the node held a value.
    pub value_present: bool,
    /// Whether the node had a destination.
    pub destination_present: bool,
    /// How many content fields the destination policy withheld.
    pub withheld_fields: u64,
}

/// What one node looks like at one destination.
#[derive(Clone, Debug, PartialEq)]
pub enum Projection {
    /// The rich local observation.
    Local(LocalProjection),
    /// The remote-model projection.
    Model(ModelProjection),
    /// The durable audit record.
    Audit(AuditProjection),
    /// The operational telemetry record.
    Telemetry(TelemetryProjection),
}

impl Projection {
    /// Which destination this projection was built for.
    pub const fn destination(&self) -> RedactionDestination {
        match self {
            Self::Local(_) => RedactionDestination::LocalContext,
            Self::Model(_) => RedactionDestination::ModelProjection,
            Self::Audit(_) => RedactionDestination::Audit,
            Self::Telemetry(_) => RedactionDestination::Telemetry,
        }
    }

    /// The classification the projection carries.
    pub const fn sensitivity(&self) -> SensitivitySet {
        match self {
            Self::Local(local) => local.sensitivity,
            Self::Model(model) => model.sensitivity,
            Self::Audit(audit) => audit.sensitivity,
            Self::Telemetry(telemetry) => telemetry.sensitivity,
        }
    }

    /// Every page-derived string this projection carries.
    ///
    /// A leak test walks this rather than a hand-written field list, so a field
    /// added to a projection without being added here fails the compile that
    /// adds it — and a field added here is one the test starts checking.
    pub fn content_fragments(&self) -> impl Iterator<Item = &str> {
        let (fields, text_runs): ([Option<&str>; 4], &[String]) = match self {
            Self::Local(local) => (
                [
                    local.name.as_deref(),
                    local.description.as_deref(),
                    local.value.as_deref(),
                    local.destination_url.as_deref(),
                ],
                &local.text_runs,
            ),
            Self::Model(model) => (
                [
                    model.name.as_deref(),
                    model.description.as_deref(),
                    model.value.as_deref(),
                    model.destination.as_deref(),
                ],
                &model.text_runs,
            ),
            Self::Audit(audit) => ([audit.destination_origin.as_deref(), None, None, None], &[]),
            // No field of the telemetry projection can hold page content.
            Self::Telemetry(_) => ([None; 4], &[]),
        };
        fields
            .into_iter()
            .flatten()
            .chain(text_runs.iter().map(String::as_str))
    }
}
