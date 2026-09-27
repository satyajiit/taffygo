// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Where an observed value is about to go, and which page-derived fields each
//! destination may carry (protocol specification section 9.3).
//!
//! Two orderings live here and they are the same ordering. [`RedactionDestination`]
//! runs from widest to narrowest, and [`RedactionDestination::permits_content`]
//! is a real containment over [`ContentField`]: every field a narrower
//! destination may carry, the wider one may carry, and each step drops at least
//! one. `crate::pipeline` walks the pairs and asserts it.
//!
//! Identifiers, enumerated names, and counts are deliberately absent from
//! [`ContentField`]. They are not page content — they are how a record is joined
//! and counted — and mixing them into the containment order would make the
//! order meaningless.

use bip_types::sensitivity::DestinationPolicy;

/// Where an observed value is about to go.
///
/// Ordered from the widest to the narrowest. The order is the containment
/// order of [`RedactionDestination::permits_content`], and nothing in this
/// module reorders it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum RedactionDestination {
    /// The isolated core task engine's rich local observation. Inside the trust
    /// boundary; nothing leaves it without passing this module again.
    LocalContext,
    /// The projection a model provider receives. Selected, minimized, and
    /// masked fields only — never the whole local observation.
    ModelProjection,
    /// A durable local audit record. Identifiers, decision facts, and
    /// normalized origins; no page text and no values.
    Audit,
    /// Operational telemetry. Enumerated names and counts only.
    Telemetry,
}

/// A page-derived content field a projection may carry.
///
/// Identifiers, enumerated names, and counts are deliberately absent: they are
/// not page content, they are how a record is joined and counted, and mixing
/// them into the containment order would make the order meaningless.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ContentField {
    /// The accessible name.
    Name,
    /// The description.
    Description,
    /// Bounded text runs.
    TextRuns,
    /// The control's normalized current value.
    NormalizedValue,
    /// The normalized origin of a destination.
    DestinationOrigin,
    /// The path of a destination.
    DestinationPath,
    /// A destination's full URL, including query and fragment.
    DestinationUrl,
}

impl ContentField {
    /// Every content field, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Name,
        Self::Description,
        Self::TextRuns,
        Self::NormalizedValue,
        Self::DestinationOrigin,
        Self::DestinationPath,
        Self::DestinationUrl,
    ];

    /// A short, compiled-in name.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Name => "name",
            Self::Description => "description",
            Self::TextRuns => "text_runs",
            Self::NormalizedValue => "normalized_value",
            Self::DestinationOrigin => "destination_origin",
            Self::DestinationPath => "destination_path",
            Self::DestinationUrl => "destination_url",
        }
    }
}

impl RedactionDestination {
    /// Every destination, widest first.
    pub const ALL: &'static [Self] = &[
        Self::LocalContext,
        Self::ModelProjection,
        Self::Audit,
        Self::Telemetry,
    ];

    /// A short, compiled-in name.
    pub const fn label(self) -> &'static str {
        match self {
            Self::LocalContext => "local_context",
            Self::ModelProjection => "model_projection",
            Self::Audit => "audit",
            Self::Telemetry => "telemetry",
        }
    }

    /// The protocol's destination policy for this destination.
    ///
    /// The lattice in `bip-types` owns what handling a classification requires;
    /// this module owns which fields exist at all. Both must agree before a
    /// value is carried, and either one alone can withhold it.
    pub const fn sensitivity_policy(self) -> DestinationPolicy {
        match self {
            Self::LocalContext => DestinationPolicy::LocalCoreService,
            Self::ModelProjection => DestinationPolicy::RemoteModel,
            Self::Audit => DestinationPolicy::LocalRecord,
            Self::Telemetry => DestinationPolicy::Telemetry,
        }
    }

    /// Whether this destination may carry `field` at all, before sensitivity is
    /// considered.
    pub const fn permits_content(self, field: ContentField) -> bool {
        match self {
            Self::LocalContext => true,
            Self::ModelProjection => !matches!(field, ContentField::DestinationUrl),
            Self::Audit => matches!(field, ContentField::DestinationOrigin),
            Self::Telemetry => false,
        }
    }

    /// Every content field this destination may carry.
    pub fn permitted_content(self) -> Vec<ContentField> {
        ContentField::ALL
            .iter()
            .copied()
            .filter(|field| self.permits_content(*field))
            .collect()
    }
}
