// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact-call and restart behavior for resident media-probe facts.

use bip_types::identity::TabId;
use model_router::catalog::WireApi;
use model_router::json::JsonValue;
use std::collections::BTreeMap;
use task_engine::{
    ActionState, HandleTable, MediaProbeTranscriptOutcome, ModelCallId, ModelReply,
    ModelStopReason, ModelToolCall, RenderShape, TurnPage, TurnResidency, TurnUsage,
};

use super::{body, RecordedCall, TaskTranscript, TranscriptBudget, TurnExchange};

fn residency(tool: &str) -> TurnResidency {
    let reply = ModelReply {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: vec![ModelToolCall::new(tool, Vec::new())],
    };
    let page = TurnPage::new(
        TabId::new("tab_1"),
        HandleTable::new(),
        RenderShape::empty([0_u8; 32]),
    );
    TurnResidency::read(ModelCallId::new("model-task_1-4"), page, reply)
        .unwrap_or_else(|| unreachable!("one bounded call"))
}

fn transcript() -> TaskTranscript {
    let call = RecordedCall::new(
        "turn-4-call-0".to_owned(),
        "media.probe".to_owned(),
        JsonValue::Object(BTreeMap::new()),
        ActionState::Verified,
    );
    TaskTranscript::new(
        "inspect the clip".to_owned(),
        vec![TurnExchange::new(4, vec![call]).unwrap_or_else(|| unreachable!("one bounded action"))],
        TranscriptBudget::default(),
    )
}

#[test]
fn probe_facts_overlay_only_the_exact_authoring_call() {
    let outcome = MediaProbeTranscriptOutcome::new(12_345, 1, 1, 1_280, 720)
        .unwrap_or_else(|_| unreachable!("bounded reading"));
    let mut exact = residency("media.probe");
    assert!(exact.record_media_probe_outcome(0, outcome.clone()));
    let mut resident = transcript();
    resident.overlay_resident_calls(&exact);
    let written = body(&resident, WireApi::AnthropicMessages);
    assert!(written.contains("duration_ms: 12345"));
    assert!(written.contains("width_px: 1280"));

    let mut wrong_tool = residency("media.audio.extract");
    assert!(!wrong_tool.record_media_probe_outcome(0, outcome));
    assert!(wrong_tool.media_probe_outcome(0).is_none());

    let restored = body(&transcript(), WireApi::AnthropicMessages);
    assert!(!restored.contains("duration_ms"));
    assert!(!restored.contains("width_px"));
}
