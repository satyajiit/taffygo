// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::*;
use bip_types::identity::{FrameId, PageEpoch, TabId};
use bip_types::snapshot::Sensitivity;
use task_engine::{
    ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence,
    SourceId as TaskSourceId,
};

use crate::context::{MediaAttachment, MediaObservation, MediaObservationKind, PageArena};
use crate::turn::ModelTurnError;

/// A router that keeps the request it was asked and plans one fixed candidate.
///
/// The answer is incidental to this suite; the argument is its whole point. A
/// preference a person expressed is only observable in what reaches route
/// selection, and this is the last place inside the process that sees it. The
/// address suite next door has the opposite interest, which is why the planned
/// candidate can be named: `None` is the catalog one these tests want.
#[derive(Debug, Default)]
pub(crate) struct RecordingRouter {
    asked: Option<RouteRequest>,
    pub(crate) plans: Option<RouteCandidate>,
    pub(crate) failover: Vec<RouteCandidate>,
}

impl ModelRouterPort for RecordingRouter {
    fn route(
        &mut self,
        request: &RouteRequest,
        _ledger: &TaskLedger,
    ) -> Result<RoutePlan, RouteRefusal> {
        self.asked = Some(request.clone());
        Ok(RoutePlan {
            route: Route::ByoDirect,
            role: ModelRole::PrimaryReasoning,
            primary: self.plans.clone().unwrap_or_else(|| candidate(true)),
            failover: self.failover.clone(),
            estimate: CostAmount {
                currency: Currency::new("USD").expect("currency"),
                micros: Micros::new(0),
                basis: PriceBasis::Metered,
            },
        })
    }

    fn context_window(&self, _model: &ModelKey) -> Option<u64> {
        Some(200_000)
    }

    fn replace_credentials(&mut self, _entries: Vec<CredentialRef>) {}

    fn replace_local_endpoints(&mut self, _endpoints: Vec<Endpoint>) {}

    fn install_catalog(&mut self, _catalog: MergedCatalog) {}

    fn set_entitlement(&mut self, _entitlement: ManagedEntitlement) {}

    fn set_policy(&mut self, _policy: ModelPolicy) {}
}

#[test]
fn a_direct_turn_retains_bounded_request_ready_substitutes() {
    let mut alternate = candidate(true);
    alternate.model = ModelKey::new(
        ProviderId::new("anthropic").expect("provider id"),
        ModelId::new("claude-haiku-5").expect("model id"),
    );
    let mut router = RecordingRouter {
        failover: vec![alternate],
        ..RecordingRouter::default()
    };
    let turn = compose_model_turn(
        &mut router,
        &NoCredentials,
        &FoldDigest,
        &facts(),
        "model-task_1-failover",
        &mut LivePage::new(),
        None,
    )
    .expect("a direct plan with a substitute composes");

    assert_eq!(turn.request.model_id, "claude-sonnet-5");
    assert_eq!(turn.failover.len(), 1);
    assert_eq!(
        turn.failover
            .front()
            .map(|attempt| attempt.request.model_id.as_str()),
        Some("claude-haiku-5")
    );
    assert_eq!(
        turn.failover
            .front()
            .map(|attempt| attempt.request.not_before_monotonic_ms),
        Some(0)
    );
}

/// A directory with nothing in it, which the fixed candidate does not need:
/// it declares no auth attachment, so no handle is looked up.
#[derive(Clone, Copy, Debug)]
pub(crate) struct NoCredentials;

impl ProviderDirectory for NoCredentials {
    fn usable_credential(&self, _provider_id: &ProviderId) -> Option<&CredentialHandle> {
        None
    }
}

pub(crate) fn facts() -> ModelTurnFacts {
    ModelTurnFacts {
        task_id: "task_1".to_owned(),
        attempts_started: 1,
        candidate_ordinal: 0,
        can_afford_model_attempt: true,
        transcript: transcript(Vec::new()),
        provider_route_id: Some("direct_user_key".to_owned()),
        tool_allowlist: Vec::new(),
        milestone: Milestone::M3,
        template_id: task_engine::TaskTemplateId::SummarizeEvidence,
        source_count: 0,
        remaining_new_source_cap: 0,
        empty_page_tab_id: None,
        discovery_tab_id: None,
        persons_pages: task_engine::PersonsPages::default(),
        activated: Vec::new(),
        nested_goal: None,
        thinking: None,
    }
}

#[test]
fn a_restored_subattempt_never_recomposes_a_different_paid_route() {
    let mut facts = facts();
    facts.attempts_started = 2;
    let mut router = RecordingRouter::default();
    let mut page = LivePage::default();
    assert_eq!(
        compose_model_turn(
            &mut router,
            &NoCredentials,
            &FoldDigest,
            &facts,
            "model-call-1",
            &mut page,
            None,
        ),
        Err(ModelTurnError::SubattemptPlanLost)
    );
    assert!(
        router.asked.is_none(),
        "a lost route plan is not selected again"
    );
}

/// The request one compose of `facts` hands to route selection.
fn asked_for(facts: &ModelTurnFacts) -> RouteRequest {
    let mut router = RecordingRouter::default();
    let mut page = LivePage::default();
    compose_model_turn(
        &mut router,
        &NoCredentials,
        &FoldDigest,
        facts,
        "model-task_1-1",
        &mut page,
        None,
    )
    .expect("a direct-route turn composes");
    router.asked.expect("the composer asked for a route")
}

#[test]
fn every_model_turn_requires_text_input() {
    assert_eq!(
        asked_for(&facts()).required_modalities,
        vec![model_router::catalog::InputModality::Text]
    );
}

#[test]
fn the_narrowed_goal_marks_only_the_nested_call_it_actually_shapes() {
    let mut nested = facts();
    nested.nested_goal = Some("inspect only the battery evidence".to_owned());
    let mut router = RecordingRouter::default();
    let nested_turn = compose_model_turn(
        &mut router,
        &NoCredentials,
        &FoldDigest,
        &nested,
        "model-task_1-nested",
        &mut LivePage::new(),
        None,
    )
    .expect("the nested turn composes");
    assert_eq!(nested_turn.nested_depth, task_engine::MAX_NESTED_DEPTH);

    let ordinary_turn = compose_model_turn(
        &mut RecordingRouter::default(),
        &NoCredentials,
        &FoldDigest,
        &nested,
        "model-task_1-person-answer",
        &mut LivePage::new(),
        Some("the person's answer goes first"),
    )
    .expect("the person-answer turn composes");
    assert_eq!(ordinary_turn.nested_depth, 0);
}

fn observation(tab: &str, origin: &str, epoch: &str) -> PageObservationEvidence {
    PageObservationEvidence {
        service_generation: 4,
        schema_version: "2.4".to_owned(),
        tab_id: TabId::new(tab),
        frame_id: FrameId::new("main"),
        page_epoch: PageEpoch::new(epoch),
        graph_revision: 7,
        normalized_origin: origin.to_owned(),
        private_profile: false,
        completeness: ObservationCompleteness::Complete,
        graph: ObservationGraphSummary {
            node_count: 0,
            relationship_count: 0,
            named_node_count: 0,
            text_run_count: 0,
            text_byte_count: 0,
        },
        total_bytes: 16,
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
fn browser_source_ids_reach_routing_but_never_the_model_body() {
    let mut page = LivePage::new();
    for (byte, tab, origin, epoch) in [
        (7, "tab-a", "https://a.example", "epoch-a"),
        (8, "tab-b", "https://b.example", "epoch-b"),
    ] {
        page.replace_source(
            TaskSourceId::from_bytes([byte; 16]),
            &observation(tab, origin, epoch),
            PageArena::new(),
        )
        .expect("canonical observation identity");
    }
    let mut router = RecordingRouter::default();
    let turn = compose_model_turn(
        &mut router,
        &NoCredentials,
        &FoldDigest,
        &facts(),
        "model-task_1-1",
        &mut page,
        None,
    )
    .expect("a multi-source direct turn composes");
    assert_eq!(
        router
            .asked
            .expect("route selection saw the context")
            .context
            .source_ids,
        vec![
            model_router::ids::SourceId::from_bytes([7; 16]),
            model_router::ids::SourceId::from_bytes([8; 16]),
        ]
    );
    let body = String::from_utf8(turn.request.request_body).expect("JSON request body");
    assert!(!body.contains("07070707070707070707070707070707"));
    assert!(!body.contains("08080808080808080808080808080808"));
}

#[test]
fn one_visual_attachment_requires_the_vision_role_and_reaches_the_effect_once() {
    const HANDLE: &str = "media-03981b7d-5408-4b11-983e-a6fe63206c93";
    let mut page = LivePage::new();
    page.replace_source_with_media(
        TaskSourceId::from_bytes([9; 16]),
        &observation("tab-vision", "https://image.example", "epoch-vision"),
        PageArena::new(),
        Some(MediaObservation {
            kind: MediaObservationKind::Image,
            facts: Vec::new(),
            attachment: Some(MediaAttachment {
                handle: HANDLE.to_owned(),
                mime_type: "image/png".to_owned(),
                width_px: 640,
                height_px: 480,
            }),
            capture_provenance: None,
            has_meaningful_text: false,
            scanned_pdf_ocr_required: false,
        }),
    )
    .expect("canonical visual observation");
    let mut router = RecordingRouter {
        failover: vec![candidate(true)],
        ..RecordingRouter::default()
    };
    let turn = compose_model_turn(
        &mut router,
        &NoCredentials,
        &FoldDigest,
        &facts(),
        "model-task_1-vision",
        &mut page,
        None,
    )
    .expect("a visual turn composes");

    let asked = router.asked.expect("the visual route request");
    assert_eq!(asked.role, ModelRole::Vision);
    assert!(asked.context.estimated_input_tokens >= MEDIA_INPUT_TOKEN_ESTIMATE);
    assert_eq!(
        asked.required_modalities,
        vec![
            model_router::catalog::InputModality::Text,
            model_router::catalog::InputModality::Image,
        ]
    );
    assert_eq!(
        turn.request.media_attachment_handle.as_deref(),
        Some(HANDLE)
    );
    assert_eq!(
        turn.request.media_attachment_mime_type.as_deref(),
        Some("image/png")
    );
    assert!(turn.failover.is_empty(), "one-use media cannot be retried");
    let body = String::from_utf8(turn.request.request_body).expect("UTF-8 provider body");
    assert_eq!(body.matches(HANDLE).count(), 1);
    assert!(body.contains(r#""media_type":"image/png""#));
}

#[test]
fn an_empty_discovery_turn_targets_only_the_prepared_tab_without_disclosing_it() {
    let mut discovery = facts();
    discovery.empty_page_tab_id = Some("discovery-tab-1".to_owned());
    let mut router = RecordingRouter::default();
    let turn = compose_model_turn(
        &mut router,
        &NoCredentials,
        &FoldDigest,
        &discovery,
        "model-task_1-1",
        &mut LivePage::new(),
        None,
    )
    .expect("an exact empty discovery turn composes");
    assert_eq!(turn.page.tab(), &TabId::new("discovery-tab-1"));
    let request = router.asked.expect("route selection saw the empty context");
    assert!(request.context.source_ids.is_empty());
    let body = String::from_utf8(turn.request.request_body).expect("JSON request body");
    assert!(!body.contains("discovery-tab-1"));
}

#[test]
fn an_invalid_discovery_tab_is_refused_before_route_selection() {
    for tab in [
        String::new(),
        "not/a/tab".to_owned(),
        "x".repeat(task_engine::MAX_OBSERVATION_IDENTIFIER_BYTES + 1),
    ] {
        let mut discovery = facts();
        discovery.empty_page_tab_id = Some(tab);
        let mut router = RecordingRouter::default();
        let result = compose_model_turn(
            &mut router,
            &NoCredentials,
            &FoldDigest,
            &discovery,
            "model-task_1-1",
            &mut LivePage::new(),
            None,
        );
        assert_eq!(result, Err(ModelTurnError::Overflow));
        assert!(router.asked.is_none());
    }
}

#[test]
fn the_rung_a_person_chose_is_the_rung_the_router_is_asked_for() {
    let mut chosen = facts();
    chosen.thinking = Some(ThinkingLevel::High);
    assert_eq!(asked_for(&chosen).thinking, ThinkingLevel::High);

    // Every rung, not just the one a surface happens to send today: the ladder
    // is the router's and this composer must not narrow it.
    for rung in model_router::thinking::LADDER {
        let mut facts = facts();
        facts.thinking = Some(rung);
        assert_eq!(asked_for(&facts).thinking, rung);
    }
}

#[test]
fn a_task_whose_rung_nobody_chose_is_asked_for_at_taffys_own() {
    // Absent means Taffy decides, and what Taffy decides is the rung this
    // composer sent when nothing could express a choice at all. A person who
    // has chosen nothing gets the behaviour they already had.
    assert_eq!(asked_for(&facts()).thinking, ThinkingLevel::Low);
}

#[test]
fn a_request_states_no_hard_pin_whatever_the_person_chose() {
    // A pinned model leads its role through the policy the provider plane
    // installs on the router, which is what keeps failover working when that
    // model is refused. A hard pin on the request would win over the catalog
    // order in every case, including that one, so this composer never sets it.
    assert_eq!(asked_for(&facts()).pinned_model, None);

    let mut chosen = facts();
    chosen.thinking = Some(ThinkingLevel::Max);
    assert_eq!(asked_for(&chosen).pinned_model, None);
}

#[test]
fn a_rung_is_the_only_thing_a_choice_moves_here() {
    let mut rung_only = facts();
    rung_only.thinking = Some(ThinkingLevel::Max);
    let asked = asked_for(&rung_only);
    assert_eq!(asked.thinking, ThinkingLevel::Max);
    assert_eq!(asked.pinned_model, None);
}
