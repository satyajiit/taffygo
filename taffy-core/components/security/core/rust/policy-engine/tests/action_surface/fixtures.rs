// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ContentDigest, DigestAlgorithm, FrameId, GraphRevision, MonotonicMillis, PageEpoch, ProfileId,
    SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;
use policy_engine::capability::{CapabilityRequest, CapabilityScope};
use policy_engine::time::SequentialIds;
use policy_engine::{
    ActionClass, ActionPhase, AllowedRedirects, ApprovalDecision, ApprovalRequest,
    ClassAvailability, ControlMode, LeaseRequest, PolicyEngine, PolicyMilestone, PolicyVersion,
    RepeatScope, RiskClass, UserGestureReceipt,
};

pub(super) const NOW: MonotonicMillis = MonotonicMillis(0);
pub(super) const LEASE_EXPIRY: MonotonicMillis = MonotonicMillis(10_000);
const CAPABILITY_EXPIRY: MonotonicMillis = MonotonicMillis(5_000);
const APPROVAL_EXPIRY: MonotonicMillis = MonotonicMillis(9_000);

/// The current fail-closed surfaces, written out here rather than read back
/// from the table this suite exists to check. M5 adds the browser-owned
/// exact-value fill and download flows; the remaining form mutations wait for
/// closed consequence classification.
const M5_SURFACE: &[&str] = &[
    "observe_page",
    "scroll_into_view",
    "open_link",
    "create_task_tab",
    "synthetic_click",
    "move_focus",
    "control_tab",
    "fill_field",
    "start_download",
    "profile_store_read",
];

const M6_SURFACE: &[&str] = &[
    "observe_page",
    "scroll_into_view",
    "open_link",
    "create_task_tab",
    "synthetic_click",
    "move_focus",
    "control_tab",
    "fill_field",
    "start_download",
    "library_read",
    "library_write",
    "memory_read",
    "memory_write",
    "profile_store_read",
];

/// Whether `milestone` authorizes `class`, from the lists written down in this
/// file.
pub(super) fn surface_authorizes(milestone: PolicyMilestone, class: ActionClass) -> bool {
    match milestone {
        PolicyMilestone::M2 | PolicyMilestone::M3 => {
            class.availability() == ClassAvailability::ReadOriented
        }
        PolicyMilestone::M5 => M5_SURFACE.contains(&class.label()),
        PolicyMilestone::M6 => M6_SURFACE.contains(&class.label()),
        PolicyMilestone::M7 => {
            M6_SURFACE.contains(&class.label()) || class == ActionClass::ExecuteToolJob
        }
    }
}

pub(super) fn engine(milestone: PolicyMilestone) -> PolicyEngine<SequentialIds> {
    let mut engine = PolicyEngine::new(SequentialIds::new(), milestone, PolicyVersion(1));
    let lease = engine.issue_lease(
        &LeaseRequest {
            task_id: TaskId::new("task_1"),
            tab_id: TabId::new("tab_1"),
            control_mode: ControlMode::Assistant,
            expires_at: LEASE_EXPIRY,
        },
        NOW,
    );
    assert!(lease.is_ok(), "the fixture lease must issue");
    engine
}

pub(super) fn scope() -> CapabilityScope {
    let origin = policy_engine::origin::normalize_serialization("https://example.test");
    let Ok(origin) = origin else {
        unreachable!("the fixture origin must normalize")
    };
    CapabilityScope {
        profile_id: ProfileId::new("profile_1"),
        tab_id: TabId::new("tab_1"),
        frame_id: FrameId::new("frame_main"),
        page_epoch: PageEpoch::new("epoch_a"),
        origin,
        node_id: Some(SemanticNodeId::new("n_1")),
        destination_scope: None,
        destination_address: None,
        required_graph_revision: GraphRevision(7),
        allowed_redirects: AllowedRedirects::none(),
    }
}

pub(super) fn proposal(class: ActionClass, ordinal: usize) -> CapabilityRequest {
    CapabilityRequest {
        task_id: TaskId::new("task_1"),
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: class,
        phase: ActionPhase::Commit,
        action_digest: ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: format!("digest_{ordinal}"),
        },
        scope: scope(),
        data_classes: SensitivitySet::EMPTY,
        context_risk: RiskClass::LocalRead,
        approval: None,
        expires_at: CAPABILITY_EXPIRY,
    }
}

/// Asks the exact question `request` implies, records a yes with a gesture, and
/// hands back the same request naming that answer.
///
/// The binding is derived from the request rather than written out, which is
/// what makes it the same question: an answer to any other one is refused by
/// the approval book.
pub(super) fn answered(
    engine: &mut PolicyEngine<SequentialIds>,
    request: &CapabilityRequest,
) -> CapabilityRequest {
    let approval_id = engine
        .present_approval(
            &ApprovalRequest {
                task_id: request.task_id.clone(),
                binding: request.approval_binding(request.effective_risk()),
                repeat_scope: RepeatScope::Once,
                expires_at: APPROVAL_EXPIRY,
            },
            NOW,
        )
        .unwrap_or_else(|_| unreachable!("the fixture approval must be presentable"));
    if engine
        .record_approval_decision(
            &approval_id,
            ApprovalDecision::Approved,
            Some(UserGestureReceipt::new("gesture_1")),
            NOW,
        )
        .is_err()
    {
        unreachable!("the fixture approval must be answerable");
    }
    let mut request = request.clone();
    request.approval = Some(approval_id);
    request
}
