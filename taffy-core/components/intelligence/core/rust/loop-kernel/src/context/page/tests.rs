// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One consented source, its canonical evidence, and a named button, shared.

mod media;
mod observation;
mod persons_page;
mod preview;

use crate::context::arena::{ArenaNode, DestinationClass};
use bip_types::identity::{FrameId, PageEpoch, TabId};
use bip_types::snapshot::{ContentTrust, SemanticRole, Sensitivity};
use task_engine::{
    ConsentedSource, ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence,
    SourceId,
};

pub(super) fn node(node_id: &str, name: &str) -> ArenaNode {
    ArenaNode {
        node_id: node_id.to_owned(),
        role: SemanticRole::Button,
        sensitivity: Sensitivity::NotSensitive,
        name: Some(name.to_owned()),
        name_withheld: false,
        actions: Vec::new(),
        states: Vec::new(),
        destination: DestinationClass::default(),
        content_trust: ContentTrust::FirstPartyDocument,
        content_signals: Vec::new(),
        text: Vec::new(),
        text_withheld: false,
        declared_text_runs: 0,
        declared_text_bytes: 0,
        container: None,
    }
}

pub(super) fn source(tab_id: &str, origin: &str) -> ConsentedSource {
    ConsentedSource {
        source_id: SourceId::from_bytes([3; 16]),
        tab_id: TabId::new(tab_id),
        normalized_origin: origin.to_owned(),
        canonical_locator: None,
    }
}

pub(super) fn evidence() -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: TabId::new("tab-1"),
        frame_id: FrameId::new("frame-1"),
        page_epoch: PageEpoch::new("epoch-1"),
        graph_revision: 3,
        normalized_origin: "https://example.test".to_owned(),
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

pub(super) fn source_id() -> SourceId {
    SourceId::from_bytes([3; 16])
}
