// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One fact, at the four gates that enforce it.
//!
//! `Model::tool_calling` says whether a model can be handed tools and answer
//! with a tool call. It is its own file because it is the one capability the
//! catalog carries, and because the interesting thing about it is not any
//! single check but that the same fact is refused at four different places for
//! four different reasons:
//!
//! | Gate | Refuses |
//! |---|---|
//! | `catalog::decode` | an entry that does not say — no `serde(default)`, no decode default |
//! | `catalog::validate` | a model cataloged for a task role that cannot call tools, on **every** merge layer |
//! | `route::RouteSelector` | selecting such a model for a call that carries tools |
//! | `wire::write_request` | writing a tool vocabulary into a body for such a model |
//!
//! The rule used to live in one place only — the baseline generator, which
//! sees the source of the embedded layer and neither of the two layers that
//! merge over it. A served overlay or a provider the user added could catalog
//! a model for a task role with no way to call one, and nothing would notice.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use model_router::catalog::{
    parse_document, validate, DefectReason, InputModality, MergedCatalog, ModelRole, ViolationRule,
    WireApi,
};
use model_router::request::{ContextManifest, DataSensitivity, RequestPurpose};
use model_router::route::{
    ModelPolicy, NoLocalEndpoints, RoutePreference, RouteRefusal, RouteRequest, RouteSelector,
};
use model_router::thinking::{ThinkingLevel, ThinkingPlan};
use model_router::wire::request::{Speaker, ToolDeclaration, Turn, WireRefusal, WireRequest};
use model_router::wire::write_request;
use model_router::TaskId;

fn planning_request(requires_tool_calling: bool) -> RouteRequest {
    RouteRequest {
        task_id: TaskId::from_bytes([1; 16]),
        role: ModelRole::PrimaryReasoning,
        preference: RoutePreference::ByoDirect,
        purpose: RequestPurpose::Planning,
        context: ContextManifest {
            source_ids: Vec::new(),
            classes: vec![DataSensitivity::Public],
            item_count: 1,
            estimated_input_tokens: 1_000,
        },
        required_modalities: vec![InputModality::Text],
        requires_tool_calling,
        thinking: ThinkingLevel::Medium,
        answer_tokens: 2_000,
        estimated_output_tokens: 500,
        pinned_model: None,
    }
}

#[test]
fn a_model_that_does_not_say_whether_it_calls_tools_is_dropped() {
    // An omitted field is a record nobody finished, and either answer invented
    // for it is a claim: false quietly withdraws a working model from every
    // task role, true routes a tool call to a model that cannot answer one.
    let text = r#"{"schema_version":1,"catalog_version":"t","generated_at":"2026-01-01T00:00:00Z",
        "providers":[],"models":[{"model_id":"m","provider_id":"p","display_name":"M",
        "roles":["PRIMARY_REASONING"],"input_modalities":["TEXT"],"reasoning":true,
        "context_window":1000,"max_output_tokens":100,
        "cost":{"snapshot_version":"s","currency":"USD","basis":"METERED",
        "input_micros_per_million":1,"output_micros_per_million":1,
        "cache_read_micros_per_million":1,"cache_write_micros_per_million":1},
        "enabled":true,"schema_version":1}]}"#;
    let parsed = parse_document(text).expect("valid JSON");
    assert!(parsed.document.models.is_empty());
    assert!(matches!(
        parsed.defects.first().map(|defect| &defect.reason),
        Some(DefectReason::MissingField {
            field: "tool_calling"
        })
    ));
}

#[test]
fn a_model_that_cannot_call_tools_is_not_cataloged_for_a_task_role() {
    let mut document = common::fixture_document();
    for model in &mut document.models {
        model.tool_calling = false;
    }
    let violations = validate(&document);
    assert!(violations.iter().any(|violation| matches!(
        violation.rule,
        ViolationRule::TaskRoleWithoutToolCalling {
            role: ModelRole::PrimaryReasoning
        }
    )));
}

#[test]
fn retrieval_is_the_one_role_the_rule_does_not_reach() {
    // Retrieval support calls nothing, which is why the role enumeration
    // answers the question rather than a list of roles kept somewhere else.
    let mut document = common::fixture_document();
    for model in &mut document.models {
        model.tool_calling = false;
        model.roles = vec![ModelRole::Embedding];
        model.reasoning = false;
    }
    let violations = validate(&document);
    assert!(!violations.iter().any(|violation| matches!(
        violation.rule,
        ViolationRule::TaskRoleWithoutToolCalling { .. }
    )));
}

#[test]
fn a_call_that_carries_tools_refuses_a_model_that_cannot_answer_one() {
    // The alternative would be sending the call without its tools, and a model
    // asked to act with no way to act answers with prose that reads like a
    // plan — a task that loops on plausible text and never does anything.
    let mut document = common::fixture_document();
    for model in &mut document.models {
        model.tool_calling = false;
    }
    let catalog = MergedCatalog::from_baseline(&document);
    let credentials = common::FixedCredentials::empty().usable(
        "effort-vendor",
        model_router::credential::AuthType::ApiKey,
        false,
    );
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let refusal = selector
        .select(&planning_request(true), &common::open_ledger())
        .expect_err("a tool-carrying call refuses");
    assert!(matches!(
        refusal,
        RouteRefusal::ToolCallingUnsupported { .. }
    ));

    // The same catalog still serves a call that hands the model no tools: the
    // precondition is the call's, not the role's.
    let plan = selector
        .select(&planning_request(false), &common::open_ledger())
        .expect("a call with no tools has no such precondition");
    assert!(!plan.primary.tool_calling);
}

#[test]
fn a_plan_carries_the_fact_the_encoder_enforces() {
    let catalog = common::fixture_catalog();
    let credentials = common::FixedCredentials::empty().usable(
        "effort-vendor",
        model_router::credential::AuthType::ApiKey,
        false,
    );
    let entitlement = common::no_entitlement();
    let policy = ModelPolicy::new();
    let selector = RouteSelector::new(
        &catalog,
        &credentials,
        &entitlement,
        &policy,
        &NoLocalEndpoints,
    );
    let plan = selector
        .select(&planning_request(true), &common::open_ledger())
        .expect("routes");
    // Carried on the candidate rather than re-read from a snapshot that may
    // since have been replaced, so the encoder and the selector cannot come to
    // disagree about one model.
    assert!(plan.primary.tool_calling);
}

#[test]
fn the_encoder_refuses_a_tool_vocabulary_the_model_cannot_answer() {
    let plan = ThinkingPlan::Disabled;
    let pieces = ["ask"];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &pieces,
    }];
    let schema = model_router::json::parse(r#"{"type":"object"}"#).unwrap();
    let tools = [ToolDeclaration {
        name: "read_page",
        description: "Read the page.",
        parameters: &schema,
    }];
    let request = WireRequest {
        model_id: "a-model",
        tool_calling: false,
        system: None,
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &tools,
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &plan,
        answer_tokens: 2_000,
        stream: false,
    };
    let mut out = String::new();
    for api in [
        WireApi::AnthropicMessages,
        WireApi::OpenAiResponses,
        WireApi::OpenAiCompletions,
        WireApi::GoogleGenerativeLanguage,
    ] {
        assert_eq!(
            write_request(api, &request, &mut out),
            Err(WireRefusal::ToolsUnsupportedByModel),
            "{api:?}"
        );
        assert!(out.is_empty(), "a refusal leaves the buffer alone");
    }
}
