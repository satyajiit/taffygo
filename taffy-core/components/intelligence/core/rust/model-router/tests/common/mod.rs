// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Shared fixtures.
//!
//! The catalog here is synthetic. It exists to exercise the schema — both
//! protocol families, both auth methods, imputed and metered pricing, a long
//! context tier, a narrowed ladder, and both kill switches — and it names no
//! real provider, because which providers ship is a product decision this crate
//! does not get to make.

#![allow(dead_code)]

use std::collections::BTreeSet;

use model_router::catalog::{CatalogDocument, MergedCatalog, WireApi};
use model_router::cost::{TaskBudget, TaskLedger};
use model_router::credential::{AuthType, CredentialDirectory, CredentialRef, CredentialState};
use model_router::ids::{ModelId, ModelKey, ProviderId, TaskId};
use model_router::json::{parse, JsonValue};
use model_router::money::{Currency, Micros};
use model_router::route::ManagedEntitlement;
use model_router::thinking::{ThinkingLevel, ThinkingPlan};
use model_router::wire::request::{Turn, WireRequest};
use model_router::wire::write_request;

/// A catalog covering every shape the decoder and the router have to handle.
pub const FIXTURE_CATALOG: &str = r#"
{
  "schema_version": 1,
  "catalog_version": "fixture-0001",
  "generated_at": "2026-08-10T12:00:00Z",
  "providers": [
    {
      "provider_id": "effort-vendor",
      "display_name": "Effort Vendor",
      "wire_api": "OPEN_AI_RESPONSES",
      "default_endpoint": "https://api.effort-vendor.example/v1",
      "auth_methods": ["API_KEY"],
      "subscription": false,
      "model_source": "STATIC_CATALOG",
      "enabled": true,
      "schema_version": 1
    },
    {
      "provider_id": "budget-vendor",
      "display_name": "Budget Vendor",
      "wire_api": "ANTHROPIC_MESSAGES",
      "default_endpoint": "https://api.budget-vendor.example",
      "auth_methods": ["API_KEY", "OAUTH"],
      "subscription": true,
      "static_headers": { "x-client-flavor": "taffy" },
      "model_source": "STATIC_CATALOG",
      "enabled": true,
      "schema_version": 1
    },
    {
      "provider_id": "retired-vendor",
      "display_name": "Retired Vendor",
      "wire_api": "OPEN_AI_COMPLETIONS",
      "default_endpoint": "https://api.retired-vendor.example",
      "auth_methods": ["API_KEY"],
      "subscription": false,
      "model_source": "DYNAMIC_LISTING",
      "enabled": false,
      "schema_version": 1
    }
  ],
  "models": [
    {
      "model_id": "reasoner-one",
      "provider_id": "effort-vendor",
      "display_name": "Reasoner One",
      "roles": ["PRIMARY_REASONING"],
      "input_modalities": ["TEXT"],
      "reasoning": true,
      "tool_calling": true,
      "thinking_levels": { "OFF": null, "MINIMAL": "minimal", "LOW": "low", "MEDIUM": "medium", "HIGH": "high" },
      "context_window": 200000,
      "max_output_tokens": 32000,
      "cost": {
        "snapshot_version": "2026-08-01",
        "currency": "USD",
        "basis": "METERED",
        "input_micros_per_million": 3000000,
        "output_micros_per_million": 15000000,
        "cache_read_micros_per_million": 300000,
        "cache_write_micros_per_million": 3750000,
        "long_context_tiers": [
          {
            "min_input_tokens": 128001,
            "input_micros_per_million": 6000000,
            "output_micros_per_million": 22500000,
            "cache_read_micros_per_million": 600000,
            "cache_write_micros_per_million": 7500000
          }
        ]
      },
      "compat": { "system_role_name": "developer" },
      "enabled": true,
      "schema_version": 1
    },
    {
      "model_id": "skimmer-one",
      "provider_id": "effort-vendor",
      "display_name": "Skimmer One",
      "roles": ["FAST_BROWSING"],
      "input_modalities": ["TEXT"],
      "reasoning": false,
      "tool_calling": true,
      "context_window": 128000,
      "max_output_tokens": 8000,
      "cost": {
        "snapshot_version": "2026-08-01",
        "currency": "USD",
        "basis": "METERED",
        "input_micros_per_million": 250000,
        "output_micros_per_million": 1250000,
        "cache_read_micros_per_million": 25000,
        "cache_write_micros_per_million": 312500
      },
      "enabled": true,
      "schema_version": 1
    },
    {
      "model_id": "reasoner-two",
      "provider_id": "budget-vendor",
      "display_name": "Reasoner Two",
      "roles": ["PRIMARY_REASONING", "VISION"],
      "input_modalities": ["TEXT", "IMAGE"],
      "reasoning": true,
      "tool_calling": true,
      "context_window": 200000,
      "max_output_tokens": 64000,
      "cost": {
        "snapshot_version": "2026-08-01",
        "currency": "USD",
        "basis": "IMPLIED",
        "input_micros_per_million": 3000000,
        "output_micros_per_million": 15000000,
        "cache_read_micros_per_million": 300000,
        "cache_write_micros_per_million": 3750000
      },
      "enabled": true,
      "schema_version": 1
    },
    {
      "model_id": "withdrawn-one",
      "provider_id": "budget-vendor",
      "display_name": "Withdrawn One",
      "roles": ["PRIMARY_REASONING"],
      "input_modalities": ["TEXT"],
      "reasoning": true,
      "tool_calling": true,
      "context_window": 100000,
      "max_output_tokens": 4000,
      "cost": {
        "snapshot_version": "2026-08-01",
        "currency": "USD",
        "basis": "IMPLIED",
        "input_micros_per_million": 1000000,
        "output_micros_per_million": 2000000,
        "cache_read_micros_per_million": 100000,
        "cache_write_micros_per_million": 1250000
      },
      "enabled": false,
      "schema_version": 1
    },
    {
      "model_id": "orphan-one",
      "provider_id": "retired-vendor",
      "display_name": "Orphan One",
      "roles": ["PRIMARY_REASONING"],
      "input_modalities": ["TEXT"],
      "reasoning": true,
      "tool_calling": true,
      "context_window": 32000,
      "max_output_tokens": 4000,
      "cost": {
        "snapshot_version": "2026-08-01",
        "currency": "USD",
        "basis": "METERED",
        "input_micros_per_million": 500000,
        "output_micros_per_million": 500000,
        "cache_read_micros_per_million": 50000,
        "cache_write_micros_per_million": 625000
      },
      "enabled": true,
      "schema_version": 1
    }
  ]
}
"#;

/// Decodes the fixture, asserting it is clean.
pub fn fixture_document() -> CatalogDocument {
    let parsed = model_router::parse_document(FIXTURE_CATALOG).expect("fixture is valid JSON");
    assert!(parsed.is_clean(), "fixture defects: {:?}", parsed.defects);
    let violations = model_router::validate(&parsed.document);
    assert!(violations.is_empty(), "fixture violations: {violations:?}");
    parsed.document
}

/// The fixture as a merged snapshot.
pub fn fixture_catalog() -> MergedCatalog {
    MergedCatalog::from_baseline(&fixture_document())
}

/// Builds a model key from two literals.
pub fn key(provider: &str, model: &str) -> ModelKey {
    ModelKey::new(
        ProviderId::new(provider).expect("provider key"),
        ModelId::new(model).expect("model key"),
    )
}

/// A credential directory backed by a fixed list.
#[derive(Debug, Default)]
pub struct FixedCredentials {
    entries: Vec<CredentialRef>,
}

impl FixedCredentials {
    /// A directory with nothing configured.
    pub fn empty() -> Self {
        Self::default()
    }

    /// Adds a usable credential.
    pub fn usable(mut self, provider: &str, auth_type: AuthType, subscription: bool) -> Self {
        self.entries.push(CredentialRef {
            provider_id: ProviderId::new(provider).expect("provider key"),
            auth_type,
            state: CredentialState::Usable,
            subscription_backed: subscription,
            created_at: None,
            rotated_at: None,
        });
        self
    }

    /// Adds a credential in a state the user has to fix.
    pub fn broken(mut self, provider: &str, auth_type: AuthType, state: CredentialState) -> Self {
        self.entries.push(CredentialRef {
            provider_id: ProviderId::new(provider).expect("provider key"),
            auth_type,
            state,
            subscription_backed: false,
            created_at: None,
            rotated_at: None,
        });
        self
    }
}

impl CredentialDirectory for FixedCredentials {
    fn credential(&self, provider_id: &ProviderId) -> CredentialRef {
        self.entries
            .iter()
            .find(|entry| &entry.provider_id == provider_id)
            .cloned()
            .unwrap_or_else(|| CredentialRef::absent(provider_id.clone(), AuthType::ApiKey))
    }
}

/// An entitlement covering the named models.
pub fn entitlement(models: &[ModelKey]) -> ManagedEntitlement {
    ManagedEntitlement {
        available: true,
        entitled_models: models.iter().cloned().collect::<BTreeSet<_>>(),
        remaining_micros: Some(Micros::new(1_000_000_000)),
        remaining_calls: Some(64),
        worker_host: "edge.taffygo.example".to_owned(),
        gateway_host: "gateway.taffygo.example".to_owned(),
    }
}

/// An entitlement that covers nothing.
pub fn no_entitlement() -> ManagedEntitlement {
    ManagedEntitlement::default()
}

/// A ledger with a generous budget.
pub fn open_ledger() -> TaskLedger {
    TaskLedger::new(
        TaskId::from_bytes([7; 16]),
        TaskBudget {
            currency: Currency::new("USD").expect("currency"),
            max_micros: Micros::new(10_000_000),
            max_calls: 32,
            max_retries: 4,
        },
    )
}

/// A ledger whose budget cannot cover anything.
pub fn tight_ledger() -> TaskLedger {
    TaskLedger::new(
        TaskId::from_bytes([9; 16]),
        TaskBudget {
            currency: Currency::new("USD").expect("currency"),
            max_micros: Micros::new(1),
            max_calls: 32,
            max_retries: 4,
        },
    )
}

// --- the protocol families --------------------------------------------------

/// Every family, so a test that walks them cannot quietly skip one.
///
/// A literal array rather than an iterator over the enumeration, because Rust
/// cannot enumerate its own types: adding a family means adding it here, and
/// the tests that walk this list are what turn that into a visible omission.
pub const ALL_FAMILIES: [WireApi; 6] = [
    WireApi::AnthropicMessages,
    WireApi::OpenAiResponses,
    WireApi::OpenAiCodexResponses,
    WireApi::OpenAiCompletions,
    WireApi::GoogleGenerativeLanguage,
    WireApi::GoogleCloudCodeAssist,
];

/// A thinking plan of whichever kind the family takes, as `thinking::plan`
/// would have built it.
pub fn plan_for(api: WireApi) -> ThinkingPlan {
    if api.is_effort_mapped() {
        ThinkingPlan::Effort {
            level: ThinkingLevel::Medium,
            value: Some("medium".to_owned()),
        }
    } else {
        ThinkingPlan::Budget {
            level: ThinkingLevel::Medium,
            thinking_tokens: 8_192,
            answer_tokens: 2_000,
        }
    }
}

/// A request carrying one instruction, the given turns, and no tools.
pub fn wire_request<'a>(plan: &'a ThinkingPlan, turns: &'a [Turn<'a>]) -> WireRequest<'a> {
    WireRequest {
        model_id: "a-model",
        tool_calling: true,
        system: Some("Be brief."),
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns,
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: plan,
        answer_tokens: 2_000,
        stream: false,
    }
}

/// The object the family's own request occupies inside a written body.
///
/// The body itself on every row that declares no envelope, and the object one
/// row nests its request in. A test that walks every family has to descend it
/// or it asserts about the wrong object — and on the row with an envelope it
/// would find nothing and pass, which is the failure this helper exists to
/// make impossible.
pub fn request_root(api: WireApi, document: &JsonValue) -> &JsonValue {
    let mut node = document;
    for key in model_router::wire::dialect_for(api).body_root {
        node = node
            .field(key)
            .unwrap_or_else(|| panic!("{api:?} wrote no {key} object"));
    }
    node
}

/// Writes a body and reads it back, so a test asserts against structure rather
/// than against a string it would have to spell twice.
pub fn wire_body(api: WireApi, request: &WireRequest<'_>) -> JsonValue {
    let mut out = String::new();
    write_request(api, request, &mut out).expect("the family writes a body");
    parse(&out).expect("the body it wrote is JSON")
}
