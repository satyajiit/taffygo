// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! That the composer writes the conversation, and not just the goal.
//!
//! The transcript has its own suite next door, and it is thorough — but it
//! builds its own [`WireRequest`] and hands it straight to `write_request`.
//! That leaves exactly one thing unasserted: whether the production composer
//! reads the transcript at all. It is a real gap rather than a theoretical
//! one. Reverting [`write_body`] to the single fixed turn it carried before
//! the transcript existed left the whole workspace suite green, because
//! nothing called [`write_body`] or [`compose_model_turn`] from a test.
//!
//! So these assert on the body [`write_body`] actually produces. A composer
//! that ignores its transcript fails here and nowhere else.
//!
//! The same gap applies to what the composer *asks the router for*, which is
//! where a person's model preference lives, so the last group asserts on the
//! [`RouteRequest`] the selector is handed rather than on the facts it was
//! built from.

use model_router::catalog::{CatalogLayer, Endpoint, ModelRole, PriceBasis, WireApi};
use model_router::cost::CostAmount;
use model_router::ids::{ModelId, ModelKey, ProviderId};
use model_router::json::JsonValue;
use model_router::money::{Currency, Micros};
use model_router::route::{
    Disclosure, DisclosureClass, EgressStatement, ManagedEntitlement, ModelPolicy, Route,
    RouteCandidate, RouteRequest,
};
use model_router::thinking::{ThinkingLevel, ThinkingPlan};
use model_router::{CredentialRef, MergedCatalog, RoutePlan, RouteRefusal, TaskLedger};
use task_engine::{ActionState, EffectiveToolSet, Milestone};

use crate::context::transcript::{RecordedCall, TaskTranscript, TranscriptBudget, TurnExchange};
use crate::context::LivePage;
use crate::ports::{ModelRouterPort, ModelTurnFacts};
use crate::provider::{CredentialHandle, ProviderDirectory};

use super::{
    compose_model_turn, disclosure_class, managed_request_id, write_body, write_managed_body,
    ModelResponseStyle, MEDIA_INPUT_TOKEN_ESTIMATE,
};
use crate::digest::{DigestError, Sha256Port};

#[path = "tests/incremental_reply.rs"]
mod incremental_reply;
#[path = "tests/managed_identity.rs"]
mod managed_identity;
#[path = "tests/managed_reply.rs"]
mod managed_reply;
#[path = "tests/route_preferences.rs"]
mod route_preferences;
#[path = "tests/tool_activation.rs"]
mod tool_activation;
#[path = "tests/tool_cache.rs"]
mod tool_cache;

/// A conversation name, in the shape the derivation produces.
///
/// A literal rather than a call to the deriver: these suites are about what a
/// body carries, and a body that carried a value this file computed the same
/// way the code under test does would agree with itself for the wrong reason.
pub(super) const TEST_CONVERSATION: &str = "0123456789abcdef0123456789abcdef";

pub(super) use managed_identity::FoldDigest;
pub(super) use route_preferences::{facts, NoCredentials, RecordingRouter};

/// One catalog candidate, shared with the address suite next door.
pub(super) fn candidate(tool_calling: bool) -> RouteCandidate {
    RouteCandidate {
        model: ModelKey::new(
            ProviderId::new("anthropic").expect("provider id"),
            ModelId::new("claude-sonnet-5").expect("model id"),
        ),
        wire_api: WireApi::AnthropicMessages,
        endpoint: Endpoint::new("https://api.anthropic.com").expect("endpoint"),
        endpoint_layer: CatalogLayer::EmbeddedBaseline,
        thinking: ThinkingPlan::Disabled,
        tool_calling,
        auth: None,
        disclosure: Disclosure {
            route: Route::ByoDirect,
            class: DisclosureClass::ApiKeyDirect,
            provider_display_name: "Anthropic".to_owned(),
            model_display_name: "Claude Sonnet 5".to_owned(),
            egress: EgressStatement {
                recipients: Vec::new(),
                classes: Vec::new(),
                source_count: 0,
            },
            price_basis: PriceBasis::Metered,
            price_snapshot_version: "test".to_owned(),
            credential: None,
        },
    }
}

fn call(ordinal: u64, sequence: u32, tool: &str) -> RecordedCall {
    RecordedCall::new(
        format!("turn-{ordinal}-call-{sequence}"),
        tool.to_owned(),
        JsonValue::Object(std::collections::BTreeMap::new()),
        ActionState::Verified,
    )
}

fn transcript(exchanges: Vec<TurnExchange>) -> TaskTranscript {
    TaskTranscript::new(
        "find the download link".to_owned(),
        exchanges,
        TranscriptBudget::default(),
    )
}

fn exchange(ordinal: u64, tool: &str) -> TurnExchange {
    TurnExchange::new(ordinal, vec![call(ordinal, 0, tool)]).expect("a turn with a call in it")
}

fn tools() -> EffectiveToolSet {
    EffectiveToolSet::for_task(Milestone::M3, &[])
}

#[test]
fn the_composer_writes_the_turns_the_transcript_carries() {
    let one = write_body(
        &candidate(true),
        &transcript(vec![exchange(1, "browser.dom.read")]),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body from one exchange");
    let two = write_body(
        &candidate(true),
        &transcript(vec![
            exchange(1, "browser.dom.read"),
            exchange(2, "browser.dom.query"),
        ]),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body from two exchanges");

    // The whole point of the phase: a second turn is a different question.
    assert_ne!(one, two);

    // And different in the specific way that matters — the later call is in
    // the later body and not in the earlier one. Comparing the bodies alone
    // would also pass if the composer varied something incidental.
    assert!(
        two.contains("turn-2-call-0"),
        "the second call reaches the body: {two}"
    );
    assert!(
        !one.contains("turn-2-call-0"),
        "and is absent before it happened: {one}"
    );
    assert!(
        one.contains("turn-1-call-0"),
        "the first call reaches the body: {one}"
    );
}

#[test]
fn the_composer_carries_the_goal_when_nothing_has_happened_yet() {
    let body = write_body(
        &candidate(true),
        &transcript(Vec::new()),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body from an empty transcript");

    assert!(
        body.contains("find the download link"),
        "the goal is the opening turn: {body}"
    );
    assert!(
        !body.contains("turn-1-call-0"),
        "and nothing else is claimed: {body}"
    );
}

#[test]
fn a_transcript_of_tool_calls_is_refused_for_a_model_that_cannot_call_tools() {
    // Not an accident of the encoder: a conversation whose history is tool
    // calls cannot be replayed to a model that has no way to express one, and
    // writing it anyway would ask the model to answer for turns it could never
    // have taken. The refusal happens before a byte reaches a provider.
    let refused = write_body(
        &candidate(false),
        &transcript(vec![exchange(1, "browser.dom.read")]),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    );
    assert!(
        refused.is_err(),
        "a tool history is not replayable to such a model"
    );

    // The same model is still perfectly able to be asked the opening question.
    let body = write_body(
        &candidate(false),
        &transcript(Vec::new()),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("an opening turn needs no tool vocabulary");
    assert!(
        body.contains("find the download link"),
        "the goal still reaches it: {body}"
    );
}

#[test]
fn a_rendered_page_reaches_the_body_and_a_turn_without_one_does_not() {
    // The mutation this exists to kill: dropping the page argument and
    // writing the transcript alone. The goal is in both bodies; the handle
    // line is only in the one that was given a page.
    let page = "[0] button \"Download\" — can activate\n";
    let with_page = write_body(
        &candidate(true),
        &transcript(Vec::new()),
        &tools(),
        Some(page),
        None,
        TEST_CONVERSATION,
    )
    .expect("a body that carries a page");
    let without = write_body(
        &candidate(true),
        &transcript(Vec::new()),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body that carries only the goal");

    assert!(
        with_page.contains(r#"[0] button \"Download\" — can activate\n"#),
        "the projection reaches the body: {with_page}"
    );
    assert!(
        !without.contains("[0]"),
        "and is absent when no page was offered: {without}"
    );
    assert_ne!(with_page, without);
}

#[test]
fn a_resident_calls_arguments_reach_the_body() {
    let mut conversation = transcript(vec![exchange(1, "browser.dom.click")]);
    conversation.overlay_resident_calls(&{
        use bip_types::identity::TabId;
        use task_engine::{
            ArgumentValue, HandleTable, ModelCallId, ModelReply, ModelStopReason, ModelToolCall,
            RenderShape, SuppliedArgument, TurnPage, TurnResidency, TurnUsage,
        };
        let reply = ModelReply {
            stop: ModelStopReason::ToolCall,
            overflow: None,
            usage: TurnUsage::default(),
            answer_segments: 0,
            tool_calls: vec![ModelToolCall::new(
                "browser.dom.click",
                vec![SuppliedArgument::new("node", ArgumentValue::Handle(7))],
            )],
        };
        let page = TurnPage::new(
            TabId::new("tab_1"),
            HandleTable::new(),
            RenderShape::empty([0_u8; 32]),
        );
        TurnResidency::read(ModelCallId::new("model-task-1"), page, reply).expect("one call")
    });
    let with_args = write_body(
        &candidate(true),
        &conversation,
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body that carries the resident arguments");
    let without = write_body(
        &candidate(true),
        &transcript(vec![exchange(1, "browser.dom.click")]),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body rebuilt from durable state alone");

    assert!(
        with_args.contains("\"node\":7"),
        "the still-resident handle reaches the body: {with_args}"
    );
    assert!(
        !without.contains("\"node\":7"),
        "and a restart that lost the residency does not invent it: {without}"
    );
}

#[test]
fn a_length_stop_resident_turn_tells_the_model_to_reissue_not_an_empty_fresh_turn() {
    // The mutation this exists to kill: composing the next turn from durable
    // state alone after a Length stop. Those calls were refused on sight, so
    // the journal never held them, and the model would be asked the same
    // opening question again. Overlaying the still-resident reply is what
    // appends Called+Returned with the re-issue sentence.
    let mut conversation = transcript(Vec::new());
    conversation.overlay_resident_calls(&{
        use bip_types::identity::TabId;
        use task_engine::{
            ArgumentValue, HandleTable, ModelCallId, ModelReply, ModelStopReason, ModelToolCall,
            RenderShape, SuppliedArgument, TurnPage, TurnResidency, TurnUsage,
        };
        let reply = ModelReply {
            stop: ModelStopReason::Length,
            overflow: None,
            usage: TurnUsage::default(),
            answer_segments: 0,
            tool_calls: vec![
                ModelToolCall::new(
                    "browser.dom.read",
                    vec![SuppliedArgument::new("node", ArgumentValue::Handle(3))],
                ),
                ModelToolCall::new(
                    "browser.dom.click",
                    vec![SuppliedArgument::new("node", ArgumentValue::Handle(7))],
                ),
            ],
        };
        let page = TurnPage::new(
            TabId::new("tab_1"),
            HandleTable::new(),
            RenderShape::empty([0_u8; 32]),
        );
        TurnResidency::read(ModelCallId::new("model-task_1-4"), page, reply).expect("two calls")
    });
    let with_overlay = write_body(
        &candidate(true),
        &conversation,
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body that carries the length-stop overlay");
    let without = write_body(
        &candidate(true),
        &transcript(Vec::new()),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body rebuilt from durable state alone");

    assert!(
        with_overlay.contains("Re-issue this tool call with complete arguments."),
        "the still-resident length-stop tells the model to re-issue: {with_overlay}"
    );
    assert!(
        with_overlay.contains("turn-4-call-0"),
        "the first truncated call reaches the body: {with_overlay}"
    );
    assert!(
        with_overlay.contains("turn-4-call-1"),
        "and the second: {with_overlay}"
    );
    assert!(
        !without.contains("Re-issue this tool call with complete arguments."),
        "a restart that lost the residency is the empty fresh turn: {without}"
    );
}

#[test]
fn a_classified_person_answer_reaches_the_opening_turn() {
    let line = "the person answered: the blue one";
    let with_answer = write_body(
        &candidate(true),
        &transcript(Vec::new()),
        &tools(),
        None,
        Some(line),
        TEST_CONVERSATION,
    )
    .expect("a body that carries a person answer");
    let without = write_body(
        &candidate(true),
        &transcript(Vec::new()),
        &tools(),
        None,
        None,
        TEST_CONVERSATION,
    )
    .expect("a body that carries only the goal");

    assert!(
        with_answer.contains(line),
        "the classified line reaches the body: {with_answer}"
    );
    assert!(
        !without.contains("the person answered"),
        "and is absent when none was supplied: {without}"
    );
}

#[test]
fn page_content_is_named_only_when_page_authored_bytes_leave() {
    assert_eq!(
        disclosure_class(true),
        core_service_types::DisclosureClass::PageContent
    );
    assert_eq!(
        disclosure_class(false),
        core_service_types::DisclosureClass::UserSelectedContent
    );
}

#[test]
fn response_style_is_bounded_plain_language_and_authority_neutral() {
    assert!(ModelResponseStyle::new(3, 0, 0, 0).is_none());
    assert!(ModelResponseStyle::new(0, 3, 0, 0).is_none());
    let instruction = ModelResponseStyle::new(2, 2, 2, 2)
        .expect("closed style")
        .system_instruction();
    assert!(instruction.len() < 1536);
    assert!(instruction.contains("trip planner"));
    assert!(instruction.contains("page content as evidence"));
    assert!(instruction.contains("verified end state"));
    assert!(instruction.contains("never guess it"));
    assert!(instruction.contains("do not change permissions"));
    assert!(instruction.contains("which tools are available"));
    assert!(!instruction.contains("preset_id"));
    assert!(!instruction.contains("configuration_id"));
}

#[test]
fn response_style_instruction_is_reused_for_the_same_closed_style() {
    let style = ModelResponseStyle::new(1, 2, 0, 1).expect("closed style");
    let first = style.system_instruction();
    let second = style.system_instruction();
    assert!(std::ptr::eq(first, second));

    let different = ModelResponseStyle::new(1, 2, 0, 2)
        .expect("closed style")
        .system_instruction();
    assert!(!std::ptr::eq(first, different));
    assert!(different.contains("Check in more often"));
}
