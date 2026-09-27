// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable, content-free evidence from one browser-validated page observation.
//!
//! The browser validates document identity, authority, and the generated Core
//! Service envelope. Rust decodes the bounded BIP graph and retains only
//! structural counts. Page text, node names, the raw graph payload, and
//! process-local clock readings are deliberately absent from this record.

use bip_types::identity::{FrameId, PageEpoch, TabId};
use bip_types::Sensitivity;

/// Largest identifier retained by a durable observation record.
pub const MAX_OBSERVATION_IDENTIFIER_BYTES: usize = 256;
/// Largest tuple-origin serialization retained by a durable observation.
pub const MAX_OBSERVATION_ORIGIN_BYTES: usize = 2_048;
/// Largest generated schema-version string retained by a durable observation.
pub const MAX_OBSERVATION_SCHEMA_VERSION_BYTES: usize = 64;

/// Whether the BIP endpoint declared the bounded graph complete.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ObservationCompleteness {
    /// Every requested field inside the reviewed bounds was represented.
    Complete,
    /// The graph is useful, but an explicit bound may affect the result.
    Incomplete,
}

impl ObservationCompleteness {
    /// Stable persistence/audit spelling.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Complete => "complete",
            Self::Incomplete => "incomplete",
        }
    }
}

/// Structural facts decoded from the BIP graph body.
///
/// A count is a fact about the observed document, not a copy of its content.
/// Keeping the summary here makes result construction replayable without
/// retaining renderer-authored bytes in the task journal.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ObservationGraphSummary {
    /// Semantic nodes in the decoded graph.
    pub node_count: u32,
    /// Directed semantic relationships in the decoded graph.
    pub relationship_count: u32,
    /// Non-sensitive nodes for which the browser allowed a non-empty name.
    pub named_node_count: u32,
    /// Text runs represented by count only.
    pub text_run_count: u64,
    /// Original text bytes represented by count only.
    pub text_byte_count: u64,
}

impl ObservationGraphSummary {
    /// Number of structural facts exposed by one source-table row.
    pub const FACT_COUNT: u64 = 5;

    /// The summary is internally possible for its decoded node count.
    pub const fn is_valid(&self) -> bool {
        self.named_node_count <= self.node_count
    }
}

/// Browser-validated observation evidence attached to one exact action result.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PageObservationEvidence {
    /// Core Service generation that accepted the correlated terminal.
    pub service_generation: u64,
    /// Generated BIP schema version encoded inside the graph.
    pub schema_version: String,
    /// Browser-owned tab identity actually observed.
    pub tab_id: TabId,
    /// Browser-owned root frame identity actually observed.
    pub frame_id: FrameId,
    /// Exact observed document lifetime.
    pub page_epoch: PageEpoch,
    /// Graph revision of that document lifetime.
    pub graph_revision: u64,
    /// Browser-committed normalized tuple origin.
    pub normalized_origin: String,
    /// Private-profile fact supplied by the browser.
    pub private_profile: bool,
    /// Explicit BIP completeness result.
    pub completeness: ObservationCompleteness,
    /// Strict Rust-decoded graph summary.
    pub graph: ObservationGraphSummary,
    /// Browser-validated encoded observation size.
    pub total_bytes: u32,
    /// Whether a declared bound truncated the observation.
    pub truncated: bool,
    /// Whether omitted material may change an answer.
    pub may_change_answer: bool,
    /// Fields removed or masked by browser validation.
    pub redacted_field_count: u32,
    /// Secret values structurally withheld.
    pub suppressed_secret_value_count: u32,
    /// Sensitive regions withheld from the graph.
    pub sensitive_zone_count: u32,
    /// Frames excluded by browser policy.
    pub policy_filtered_frame_count: u32,
    /// Highest browser-validated sensitivity represented.
    pub highest_sensitivity: Sensitivity,
}

impl PageObservationEvidence {
    /// Checks invariants that must survive persistence and replay.
    pub fn is_valid(&self) -> bool {
        self.service_generation > 0
            && bounded_non_empty(&self.schema_version, MAX_OBSERVATION_SCHEMA_VERSION_BYTES)
            && bounded_non_empty(&self.tab_id.0, MAX_OBSERVATION_IDENTIFIER_BYTES)
            && bounded_non_empty(&self.frame_id.0, MAX_OBSERVATION_IDENTIFIER_BYTES)
            && bounded_non_empty(&self.page_epoch.0, MAX_OBSERVATION_IDENTIFIER_BYTES)
            && bounded_non_empty(&self.normalized_origin, MAX_OBSERVATION_ORIGIN_BYTES)
            && self.graph_revision > 0
            && self.total_bytes > 0
            && self.graph.is_valid()
            // A complete reading may not also say it was truncated or that its
            // answer may change. The converse is not an invariant and must not
            // be written as one: a reading is also incomplete when an adapter
            // could not fully report, and that sets neither flag (decision
            // 0171).
            && !(self.completeness == ObservationCompleteness::Complete
                && (self.truncated || self.may_change_answer))
    }

    /// Whether this evidence can support a result with no labelled gap.
    pub fn supports_complete_result(&self) -> bool {
        self.is_valid() && self.completeness == ObservationCompleteness::Complete
    }
}

fn bounded_non_empty(value: &str, maximum: usize) -> bool {
    !value.is_empty() && value.len() <= maximum
}

#[cfg(test)]
mod tests {
    use super::{ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence};
    use bip_types::identity::{FrameId, PageEpoch, TabId};
    use bip_types::Sensitivity;

    fn evidence() -> PageObservationEvidence {
        PageObservationEvidence {
            service_generation: 3,
            schema_version: "2.4".to_owned(),
            tab_id: TabId("tab-1".to_owned()),
            frame_id: FrameId("frame-1".to_owned()),
            page_epoch: PageEpoch("epoch-1".to_owned()),
            graph_revision: 7,
            normalized_origin: "https://example.test".to_owned(),
            private_profile: false,
            completeness: ObservationCompleteness::Complete,
            graph: ObservationGraphSummary {
                node_count: 3,
                relationship_count: 2,
                named_node_count: 2,
                text_run_count: 4,
                text_byte_count: 80,
            },
            total_bytes: 120,
            truncated: false,
            may_change_answer: false,
            redacted_field_count: 0,
            suppressed_secret_value_count: 0,
            sensitive_zone_count: 0,
            policy_filtered_frame_count: 0,
            highest_sensitivity: Sensitivity::NotSensitive,
        }
    }

    #[test]
    fn complete_evidence_is_bounded_and_not_truncated() {
        let evidence = evidence();
        assert!(evidence.is_valid());
        assert!(evidence.supports_complete_result());
    }

    #[test]
    fn an_incomplete_reading_needs_no_budget_to_blame() {
        // This test used to assert the opposite - that incompleteness without
        // a truncation flag is invalid - and that assertion was wrong about
        // the browser it describes. An adapter that cannot fully report makes
        // the reading incomplete on its own and sets neither flag, so the rule
        // refused a perfectly good reading of a real site and ended the task
        // holding it (decision 0171).
        let mut evidence = evidence();
        evidence.completeness = ObservationCompleteness::Incomplete;
        assert!(evidence.is_valid());
        assert!(!evidence.supports_complete_result());

        evidence.truncated = true;
        assert!(evidence.is_valid());
        assert!(!evidence.supports_complete_result());
    }

    #[test]
    fn a_complete_reading_may_not_also_say_it_was_cut_short() {
        for (truncated, may_change_answer) in [(true, false), (false, true), (true, true)] {
            let mut evidence = evidence();
            evidence.truncated = truncated;
            evidence.may_change_answer = may_change_answer;
            assert!(!evidence.is_valid(), "{truncated} {may_change_answer}");
        }
    }

    #[test]
    fn impossible_named_node_count_is_refused() {
        let mut evidence = evidence();
        evidence.graph.named_node_count = evidence.graph.node_count.saturating_add(1);
        assert!(!evidence.is_valid());
    }
}
