// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Invariants a catalog must satisfy before anything routes against it.
//!
//! Decoding answers "is this entry well-formed?". Validation answers "does this
//! set of entries describe a world the router can act in?" — a model whose
//! provider is absent, a ladder no rung of which is supported, a price tier
//! order that makes tier selection ambiguous. These are the failures that would
//! otherwise surface as a refusal deep inside route selection, where the
//! operator cannot see what caused them.
//!
//! Validation reports; it does not repair. A repaired catalog is a catalog
//! nobody published.

use crate::catalog::types::{
    CatalogDocument, InputModality, Model, ModelRole, PriceBasis, Provider,
    SUPPORTED_SCHEMA_VERSION,
};
use crate::ids::{ModelKey, ProviderId};
use crate::thinking::{ThinkingLevel, LADDER};

/// Which rule a document broke.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ViolationRule {
    /// The document header declared an unsupported schema version.
    UnsupportedDocumentSchema {
        /// The declared version.
        declared: u32,
    },
    /// The document header carried no catalog version.
    MissingCatalogVersion,
    /// A model named a provider the document does not define.
    UnknownProvider {
        /// The named provider.
        provider_id: ProviderId,
    },
    /// A model's ladder supports no rung, so clamping could not answer.
    NoSupportedThinkingLevel,
    /// A model that produces no thinking output cannot turn thinking off.
    NonReasoningModelCannotDisableThinking,
    /// A model declared a zero or contradictory token limit.
    ImpossibleTokenLimits {
        /// The declared context window.
        context_window: u64,
        /// The declared output allowance.
        max_output_tokens: u64,
    },
    /// Long-context tiers were not strictly ascending, or started at zero.
    UnorderedPriceTiers,
    /// A price snapshot was imputed for a provider with no plan behind it.
    ImpliedPriceWithoutSubscription,
    /// A model cataloged for the image role does not accept images.
    VisionRoleWithoutImageInput,
    /// A model cataloged for a text role does not accept text.
    TextRoleWithoutTextInput {
        /// The role in question.
        role: ModelRole,
    },
    /// A model cataloged for a task role cannot call tools.
    ///
    /// Task work is tool-driven, so this entry describes a model the router
    /// would select and then be unable to give a tool vocabulary to. The rule
    /// is here rather than in the baseline generator because the generator
    /// only ever sees the source of the embedded layer: an overlay served to
    /// the device and a provider the user added themselves reach the router
    /// without passing through it, and this is the only gate all three share.
    TaskRoleWithoutToolCalling {
        /// The role in question.
        role: ModelRole,
    },
}

/// One broken rule and the entry that broke it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CatalogViolation {
    /// The model the rule is about, when it is about one.
    pub model: Option<ModelKey>,
    /// The provider the rule is about, when it is about one.
    pub provider: Option<ProviderId>,
    /// The rule.
    pub rule: ViolationRule,
}

/// Checks every documented invariant.
///
/// An empty result means the document is routable. It does not mean the
/// document offers anything: an empty catalog is valid and routes nothing,
/// which is the correct state for a build whose provider list is not yet
/// decided.
pub fn validate(document: &CatalogDocument) -> Vec<CatalogViolation> {
    let mut violations = Vec::new();
    if document.header.schema_version != SUPPORTED_SCHEMA_VERSION {
        violations.push(CatalogViolation {
            model: None,
            provider: None,
            rule: ViolationRule::UnsupportedDocumentSchema {
                declared: document.header.schema_version,
            },
        });
    }
    if document.header.catalog_version.is_empty() {
        violations.push(CatalogViolation {
            model: None,
            provider: None,
            rule: ViolationRule::MissingCatalogVersion,
        });
    }
    for model in &document.models {
        validate_model(model, &document.providers, &mut violations);
    }
    violations
}

fn validate_model(model: &Model, providers: &[Provider], out: &mut Vec<CatalogViolation>) {
    let key = model.key();
    let mut push = |rule: ViolationRule| {
        out.push(CatalogViolation {
            model: Some(key.clone()),
            provider: Some(model.provider_id.clone()),
            rule,
        });
    };
    let provider = providers
        .iter()
        .find(|candidate| candidate.provider_id == model.provider_id);
    let Some(provider) = provider else {
        push(ViolationRule::UnknownProvider {
            provider_id: model.provider_id.clone(),
        });
        return;
    };
    if !LADDER
        .into_iter()
        .any(|level| model.thinking_levels.support(level).is_supported())
    {
        push(ViolationRule::NoSupportedThinkingLevel);
    }
    if !model.reasoning
        && !model
            .thinking_levels
            .support(ThinkingLevel::Off)
            .is_supported()
    {
        push(ViolationRule::NonReasoningModelCannotDisableThinking);
    }
    if model.context_window == 0
        || model.max_output_tokens == 0
        || model.max_output_tokens > model.context_window
    {
        push(ViolationRule::ImpossibleTokenLimits {
            context_window: model.context_window,
            max_output_tokens: model.max_output_tokens,
        });
    }
    if !tiers_ascending(model) {
        push(ViolationRule::UnorderedPriceTiers);
    }
    if model.cost.basis == PriceBasis::Implied && !provider.subscription {
        push(ViolationRule::ImpliedPriceWithoutSubscription);
    }
    if model.serves(ModelRole::Vision) && !model.accepts(InputModality::Image) {
        push(ViolationRule::VisionRoleWithoutImageInput);
    }
    for role in [
        ModelRole::PrimaryReasoning,
        ModelRole::FastBrowsing,
        ModelRole::Embedding,
    ] {
        if model.serves(role) && !model.accepts(InputModality::Text) {
            push(ViolationRule::TextRoleWithoutTextInput { role });
        }
    }
    if !model.tool_calling {
        for role in &model.roles {
            if role.requires_tool_calling() {
                push(ViolationRule::TaskRoleWithoutToolCalling { role: *role });
            }
        }
    }
}

fn tiers_ascending(model: &Model) -> bool {
    let mut previous = 0u64;
    for tier in &model.cost.long_context_tiers {
        if tier.min_input_tokens == 0 || tier.min_input_tokens <= previous {
            return false;
        }
        previous = tier.min_input_tokens;
    }
    true
}
