// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One observation and one already-redacted node, shared by every arena test.

mod bounds;
mod containment;
mod identity;
mod query;

use super::{ArenaNode, ArenaTextRun, DestinationClass};
use bip_types::identity::{FrameId, PageEpoch, TabId};
use bip_types::snapshot::{ContentTrust, SemanticRole, Sensitivity};
use task_engine::observation::{
    ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence,
};

pub(super) fn evidence(origin: &str) -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "0.7".to_owned(),
        tab_id: TabId::new("tab-1"),
        frame_id: FrameId::new("frame-1"),
        page_epoch: PageEpoch::new("epoch-1"),
        graph_revision: 7,
        normalized_origin: origin.to_owned(),
        private_profile: false,
        completeness: ObservationCompleteness::Complete,
        graph: ObservationGraphSummary {
            node_count: 1,
            relationship_count: 0,
            named_node_count: 1,
            text_run_count: 0,
            text_byte_count: 0,
        },
        total_bytes: 64,
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::NotSensitive,
    }
}

pub(super) fn node(node_id: &str, name: Option<&str>, text: &[&str]) -> ArenaNode {
    ArenaNode {
        node_id: node_id.to_owned(),
        role: SemanticRole::Paragraph,
        sensitivity: Sensitivity::NotSensitive,
        name: name.map(ToOwned::to_owned),
        name_withheld: false,
        actions: Vec::new(),
        states: Vec::new(),
        destination: DestinationClass::default(),
        content_trust: ContentTrust::FirstPartyDocument,
        content_signals: Vec::new(),
        text: text
            .iter()
            .map(|run| ArenaTextRun {
                text: (*run).to_owned(),
                content_trust: ContentTrust::FirstPartyDocument,
                content_signals: Vec::new(),
            })
            .collect(),
        text_withheld: false,
        declared_text_runs: u32::try_from(text.len()).unwrap_or(u32::MAX),
        declared_text_bytes: text.iter().map(|run| run.len() as u64).sum(),
        container: None,
    }
}
