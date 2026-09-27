// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The redaction pipeline end to end (protocol specification section 9, data
//! and privacy sections 7 and 14, work package WP-M2-06).
//!
//! [`crate::redaction::redact_for`] answers "what may this one destination be
//! told?". This module answers the question the milestone exit review actually
//! asks: **are the four destinations really ordered?**
//!
//! [`project_all`] classifies once and projects four times, and
//! [`DestinationSet::verify_narrowing`] then proves five things about the
//! result, none of which is a restatement of the field table it is checking:
//!
//! 1. **The field sets nest strictly.** Every content field a narrower
//!    destination may carry, the wider one may carry, and each step drops at
//!    least one.
//! 2. **The carried content shrinks.** No narrower projection carries more
//!    page-derived strings than the one before it.
//! 3. **Nothing is invented.** Every span a narrower projection carries, minus
//!    the masked ones, occurs in the local observation. A narrower destination
//!    can only remove.
//! 4. **The handling only tightens.** The lattice's own verdict for each
//!    destination is at least as strict as for the one before it, so the two
//!    independent layers cannot disagree about the direction.
//! 5. **Telemetry carries nothing.** The narrowest destination holds no
//!    page-derived string at all.
//!
//! The last layer of the pipeline is not here. `audit-engine` redacts again,
//! from its own field policy, sharing no code with these serializers, because a
//! final pass that trusts the pass before it is not a pass.

// `Handling` is ordered from permissive to strict in `bip-types`, so the
// direction check below is a comparison rather than a table of its own.
use bip_types::sensitivity::SensitivitySet;

use crate::redaction::{
    classify_zone, redact_classified, AuditProjection, ContentField, FieldObservation,
    LocalProjection, ModelProjection, Projection, RedactionDestination, TelemetryProjection,
    ZoneClassification,
};

/// One observation, projected for every destination.
#[derive(Clone, Debug, PartialEq)]
pub struct DestinationSet {
    classification: ZoneClassification,
    local: Projection,
    model: Projection,
    audit: Projection,
    telemetry: Projection,
}

impl DestinationSet {
    /// Assembles a set from projections a caller already holds.
    ///
    /// [`project_all`] is how the pipeline is normally run. This exists so
    /// [`Self::verify_narrowing`] can be shown to fail: a checker that has only
    /// ever been given correct input has not been checked. Nothing here
    /// authorizes anything, so a set assembled by hand grants nothing either.
    pub const fn from_projections(
        classification: ZoneClassification,
        local: Projection,
        model: Projection,
        audit: Projection,
        telemetry: Projection,
    ) -> Self {
        Self {
            classification,
            local,
            model,
            audit,
            telemetry,
        }
    }

    /// The classification every projection was built from.
    ///
    /// One classification, four destinations: what a value *is* does not depend
    /// on who is asking, only what may be carried does.
    pub const fn classification(&self) -> &ZoneClassification {
        &self.classification
    }

    /// The projection built for `destination`.
    pub const fn projection(&self, destination: RedactionDestination) -> &Projection {
        match destination {
            RedactionDestination::LocalContext => &self.local,
            RedactionDestination::ModelProjection => &self.model,
            RedactionDestination::Audit => &self.audit,
            RedactionDestination::Telemetry => &self.telemetry,
        }
    }

    /// The rich local observation.
    pub const fn local(&self) -> Option<&LocalProjection> {
        match &self.local {
            Projection::Local(local) => Some(local),
            _ => None,
        }
    }

    /// The projection a model provider receives.
    pub const fn model(&self) -> Option<&ModelProjection> {
        match &self.model {
            Projection::Model(model) => Some(model),
            _ => None,
        }
    }

    /// The durable audit record.
    pub const fn audit(&self) -> Option<&AuditProjection> {
        match &self.audit {
            Projection::Audit(audit) => Some(audit),
            _ => None,
        }
    }

    /// The operational telemetry record.
    pub const fn telemetry(&self) -> Option<&TelemetryProjection> {
        match &self.telemetry {
            Projection::Telemetry(telemetry) => Some(telemetry),
            _ => None,
        }
    }

    /// Every page-derived string carried to `destination`.
    pub fn fragments(&self, destination: RedactionDestination) -> impl Iterator<Item = &str> {
        self.projection(destination).content_fragments()
    }

    /// Proves the four destinations are strictly ordered.
    pub fn verify_narrowing(&self) -> Result<(), NarrowingBreach> {
        let local_content = self.concatenated_local_content();
        let mut previous: Option<RedactionDestination> = None;

        for destination in RedactionDestination::ALL {
            let destination = *destination;
            self.check_classification_is_unchanged(destination)?;
            self.check_nothing_is_invented(destination, &local_content)?;

            if let Some(wider) = previous {
                check_field_sets_nest(wider, destination)?;
                self.check_content_shrinks(wider, destination)?;
                self.check_handling_tightens(wider, destination)?;
            }
            previous = Some(destination);
        }

        if self
            .fragments(RedactionDestination::Telemetry)
            .next()
            .is_some()
        {
            return Err(NarrowingBreach::TelemetryCarriesContent);
        }
        Ok(())
    }

    /// Every content field the local observation held, lowercased and joined.
    fn concatenated_local_content(&self) -> String {
        let mut content = String::new();
        for fragment in self.fragments(RedactionDestination::LocalContext) {
            if !content.is_empty() {
                content.push('\u{1f}');
            }
            content.extend(fragment.chars().flat_map(char::to_lowercase));
        }
        content
    }

    /// The classification is a property of the field, not of the destination.
    fn check_classification_is_unchanged(
        &self,
        destination: RedactionDestination,
    ) -> Result<(), NarrowingBreach> {
        let carried = self.projection(destination).sensitivity();
        if carried == self.classification.sensitivity() {
            Ok(())
        } else {
            Err(NarrowingBreach::ClassificationChanged {
                destination,
                classified: self.classification.sensitivity(),
                carried,
            })
        }
    }

    /// A narrower destination removes; it never adds.
    ///
    /// Comparison is over alphanumeric runs rather than whole strings, because
    /// a narrower destination legitimately *rewrites* what it keeps: a URL
    /// becomes a normalized origin with its default port dropped and its host
    /// lowercased, and a masked span becomes a fixed marker. Neither is a
    /// substring of what it came from, and both are removals. A run the local
    /// observation never held is not.
    fn check_nothing_is_invented(
        &self,
        destination: RedactionDestination,
        local_content: &str,
    ) -> Result<(), NarrowingBreach> {
        if destination == RedactionDestination::LocalContext {
            return Ok(());
        }
        let mut lowered = String::new();
        for fragment in self.fragments(destination) {
            for span in fragment.split(crate::redaction::MASK_MARKER) {
                for run in span.split(|character: char| !character.is_alphanumeric()) {
                    if run.is_empty() {
                        continue;
                    }
                    lowered.clear();
                    lowered.extend(run.chars().flat_map(char::to_lowercase));
                    if !local_content.contains(&lowered) {
                        return Err(NarrowingBreach::ContentInvented { destination });
                    }
                }
            }
        }
        Ok(())
    }

    /// No narrower destination carries more strings than the wider one.
    fn check_content_shrinks(
        &self,
        wider: RedactionDestination,
        narrower: RedactionDestination,
    ) -> Result<(), NarrowingBreach> {
        let wider_count = self.fragments(wider).count();
        let narrower_count = self.fragments(narrower).count();
        if narrower_count <= wider_count {
            Ok(())
        } else {
            Err(NarrowingBreach::ContentGrew { wider, narrower })
        }
    }

    /// The lattice's verdict tightens in the same direction as the field table.
    fn check_handling_tightens(
        &self,
        wider: RedactionDestination,
        narrower: RedactionDestination,
    ) -> Result<(), NarrowingBreach> {
        let wider_handling = self.classification.handling_for(wider);
        let narrower_handling = self.classification.handling_for(narrower);
        if narrower_handling >= wider_handling {
            Ok(())
        } else {
            Err(NarrowingBreach::HandlingRelaxed { wider, narrower })
        }
    }
}

/// Why the destinations are not strictly ordered.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NarrowingBreach {
    /// A narrower destination may carry a field the wider one may not.
    FieldSetWidened {
        /// The wider destination.
        wider: RedactionDestination,
        /// The narrower destination.
        narrower: RedactionDestination,
        /// The field that escaped.
        field: ContentField,
    },
    /// Two destinations carry exactly the same fields, so one of them is not
    /// narrower than the other.
    FieldSetNotStrict {
        /// The wider destination.
        wider: RedactionDestination,
        /// The narrower destination.
        narrower: RedactionDestination,
    },
    /// A narrower destination carries more page-derived strings.
    ContentGrew {
        /// The wider destination.
        wider: RedactionDestination,
        /// The narrower destination.
        narrower: RedactionDestination,
    },
    /// A narrower destination carries a span the local observation never held.
    ContentInvented {
        /// The destination that invented it.
        destination: RedactionDestination,
    },
    /// The lattice is more permissive for a narrower destination.
    HandlingRelaxed {
        /// The wider destination.
        wider: RedactionDestination,
        /// The narrower destination.
        narrower: RedactionDestination,
    },
    /// A projection carries a classification other than the one that was
    /// decided.
    ClassificationChanged {
        /// Where it changed.
        destination: RedactionDestination,
        /// What the classifier decided.
        classified: SensitivitySet,
        /// What the projection carried.
        carried: SensitivitySet,
    },
    /// The telemetry projection carried a page-derived string.
    TelemetryCarriesContent,
}

impl NarrowingBreach {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::FieldSetWidened { .. } => "field_set_widened",
            Self::FieldSetNotStrict { .. } => "field_set_not_strict",
            Self::ContentGrew { .. } => "content_grew",
            Self::ContentInvented { .. } => "content_invented",
            Self::HandlingRelaxed { .. } => "handling_relaxed",
            Self::ClassificationChanged { .. } => "classification_changed",
            Self::TelemetryCarriesContent => "telemetry_carries_content",
        }
    }
}

/// Classifies once and projects for every destination.
pub fn project_all(observation: &FieldObservation) -> DestinationSet {
    let classification = classify_zone(observation);
    DestinationSet {
        local: redact_classified(
            observation,
            &classification,
            RedactionDestination::LocalContext,
        ),
        model: redact_classified(
            observation,
            &classification,
            RedactionDestination::ModelProjection,
        ),
        audit: redact_classified(observation, &classification, RedactionDestination::Audit),
        telemetry: redact_classified(
            observation,
            &classification,
            RedactionDestination::Telemetry,
        ),
        classification,
    }
}

/// Whether the narrower destination's field set is a strict subset.
fn check_field_sets_nest(
    wider: RedactionDestination,
    narrower: RedactionDestination,
) -> Result<(), NarrowingBreach> {
    let mut dropped = false;
    for field in ContentField::ALL {
        match (
            wider.permits_content(*field),
            narrower.permits_content(*field),
        ) {
            (false, true) => {
                return Err(NarrowingBreach::FieldSetWidened {
                    wider,
                    narrower,
                    field: *field,
                })
            }
            (true, false) => dropped = true,
            _ => {}
        }
    }
    if dropped {
        Ok(())
    } else {
        Err(NarrowingBreach::FieldSetNotStrict { wider, narrower })
    }
}
