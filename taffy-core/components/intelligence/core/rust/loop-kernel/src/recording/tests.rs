// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use crate::context::{ArenaNode, PageArena};
use bip_types::identity::{ContentDigest, DigestAlgorithm, FrameId, PageEpoch, TabId};
use bip_types::snapshot::{ContentTrust, SemanticRole, Sensitivity};
use task_engine::action::{ActionIntent, BrowserIntent, ObservedNodeHandle};
use task_engine::{
    ActionProposal, BrowserSessionId, ControlMode, IdempotencyKey, ObservationCompleteness,
    ObservationGraphSummary, PageObservationEvidence, PolicyVersion, SourceId,
};

mod discovery;

fn page() -> (LivePage, ObservedNodeHandle) {
    page_at("https://example.test")
}

fn page_at(origin: &str) -> (LivePage, ObservedNodeHandle) {
    let mut arena = PageArena::new();
    for (id, role, name) in [
        ("root", SemanticRole::Document, "Page"),
        ("live-download-node", SemanticRole::Link, "Download"),
    ] {
        assert!(arena.push(ArenaNode {
            node_id: id.into(),
            role,
            sensitivity: Sensitivity::NotSensitive,
            name: Some(name.into()),
            name_withheld: false,
            actions: vec![],
            states: vec![],
            destination: crate::context::arena::DestinationClass::default(),
            content_trust: ContentTrust::FirstPartyDocument,
            content_signals: vec![],
            text: vec![],
            text_withheld: false,
            declared_text_runs: 0,
            declared_text_bytes: 0,
            container: None,
        }));
    }
    let evidence = PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".into(),
        tab_id: TabId::new("tab"),
        frame_id: FrameId::new("frame"),
        page_epoch: PageEpoch::new("after-person"),
        graph_revision: 4,
        normalized_origin: origin.into(),
        private_profile: false,
        completeness: ObservationCompleteness::Complete,
        graph: ObservationGraphSummary {
            node_count: 2,
            relationship_count: 0,
            named_node_count: 2,
            text_run_count: 0,
            text_byte_count: 0,
        },
        total_bytes: 256,
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: Sensitivity::NotSensitive,
    };
    let target = ObservedNodeHandle::from_node_handle(
        &crate::context::PageIdentity::from_evidence(&evidence)
            .unwrap()
            .node_handle("live-download-node"),
    )
    .unwrap();
    let mut page = LivePage::new();
    page.replace_source(SourceId::from_bytes([3; 16]), &evidence, arena)
        .unwrap();
    (page, target)
}

fn facts(id: &str, intent: BrowserIntent) -> ActionEffectFacts {
    ActionEffectFacts {
        action_id: id.into(),
        proposal: ActionProposal::new(
            ActionIntent::Browser(intent),
            None,
            IdempotencyKey::new(id),
            true,
            None,
            ContentDigest {
                algorithm: DigestAlgorithm::Sha256,
                value: "a".repeat(64),
            },
        ),
        state: ActionState::Verified,
        capability_id: Some("cap".into()),
        dispatch_id: Some("dispatch".into()),
        approval: None,
        control_mode: ControlMode::Assistant,
        policy_version: PolicyVersion(1),
        skill_version_id: None,
    }
}

fn source() -> ConsentedSource {
    ConsentedSource {
        source_id: SourceId::from_bytes([3; 16]),
        tab_id: TabId::new("tab"),
        normalized_origin: "https://example.test".into(),
        canonical_locator: Some("https://example.test/entry".into()),
    }
}

fn recorded_flow() -> FlowRecording {
    let mut recording = FlowRecording::default();
    recording.begin();
    let navigate = facts(
        "navigate",
        BrowserIntent::Navigate {
            tab: TabId::new("tab"),
            address: "https://example.test/entry".into(),
            new_tab: false,
        },
    );
    recording.dispatched(&navigate, &LivePage::new());
    recording.settled(&navigate, Some(&source()), None);
    recording.handover("person", false);
    recording.handover("person", true);
    let (page, target) = page();
    let download = facts(
        "download",
        BrowserIntent::DownloadFromLink {
            target,
            browser_session_id: BrowserSessionId::new("session").unwrap(),
        },
    );
    recording.dispatched(&download, &page);
    recording.settled(&download, Some(&source()), None);
    recording
}

fn finished(recording: &FlowRecording) -> Option<Procedure> {
    recording.finish(ProcedureId::new("flow.example").unwrap(), "task-exact")
}

#[test]
fn verified_flow_keeps_references_and_person_step_in_reviewable_draft() {
    let procedure = finished(&recorded_flow()).unwrap();
    assert_eq!(procedure.status, procedure_engine::ProcedureStatus::Draft);
    assert_eq!(
        procedure.recorded_from_task_id.as_deref(),
        Some("task-exact")
    );
    assert_eq!(procedure.steps.len(), 3);
    assert_eq!(procedure.steps[1].verb, "user.handover");
    let bytes = procedure_engine::definition::encode(&procedure).unwrap();
    assert!(!bytes
        .windows(b"live-download-node".len())
        .any(|window| window == b"live-download-node"));
    assert!(!bytes
        .windows(b"after-person".len())
        .any(|window| window == b"after-person"));
}

#[test]
fn unknown_step_missing_terminal_and_different_origin_refuse_whole_flow() {
    let mut missing = recorded_flow();
    missing.handover("another-person", false);
    assert!(finished(&missing).is_none());
    let mut different = recorded_flow();
    different.observe_origin("https://other.test");
    assert!(finished(&different).is_none());
    let mut unknown = recorded_flow();
    let unserved = facts(
        "unknown",
        BrowserIntent::Reload {
            tab: TabId::new("tab"),
        },
    );
    unknown.dispatched(&unserved, &page().0);
    unknown.settled(&unserved, Some(&source()), None);
    assert!(finished(&unknown).is_none());
    assert!(unknown.steps.is_empty());
}

#[test]
fn stale_node_and_restored_residency_cannot_become_a_recording() {
    let mut recording = recorded_flow();
    let (mut page, target) = page();
    page.invalidate_observations();
    let stale = facts(
        "stale",
        BrowserIntent::DownloadFromLink {
            target,
            browser_session_id: BrowserSessionId::new("session").unwrap(),
        },
    );
    recording.dispatched(&stale, &page);
    recording.settled(&stale, Some(&source()), None);
    assert!(finished(&recording).is_none());
    let mut restored = FlowRecording::default();
    restored.handover("person", false);
    restored.handover("person", true);
    assert!(finished(&restored).is_none());
}
