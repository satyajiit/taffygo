// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Decoding one model entry: its roles, modalities, thinking ladder, and price.
//!
//! The four closed enumerations a model carries are decoded here rather than
//! by the generic field readers, because each has its own published spelling
//! and an unknown value in any of them drops the model instead of widening
//! what the product will accept.

use super::defect::DefectReason;
use super::field::{
    bounded_name, required, required_bool, required_str, required_u64, schema_version, string_map,
    wire_api,
};
use super::limits::{MAX_OVERRIDES, MAX_OVERRIDE_LEN, MAX_PRICE_TIERS};
use crate::catalog::types::{
    Endpoint, InputModality, LongContextTier, Model, ModelRole, PriceBasis, PriceSnapshot,
};
use crate::ids::{ModelId, ProviderId};
use crate::json::JsonValue;
use crate::money::Currency;
use crate::thinking::{ThinkingLevel, ThinkingLevels, LADDER};
pub(super) fn decode_roles(value: &JsonValue) -> Result<Vec<ModelRole>, DefectReason> {
    let items = required(value, "roles")?
        .as_array()
        .ok_or(DefectReason::WrongType { field: "roles" })?;
    let mut out = Vec::new();
    for item in items {
        let text = item
            .as_str()
            .ok_or(DefectReason::WrongType { field: "roles" })?;
        let role = match text {
            "PRIMARY_REASONING" => ModelRole::PrimaryReasoning,
            "FAST_BROWSING" => ModelRole::FastBrowsing,
            "VISION" => ModelRole::Vision,
            "EMBEDDING" => ModelRole::Embedding,
            other => {
                return Err(DefectReason::UnknownEnumValue {
                    field: "roles",
                    value: other.to_owned(),
                })
            }
        };
        if !out.contains(&role) {
            out.push(role);
        }
    }
    if out.is_empty() {
        return Err(DefectReason::Invalid {
            field: "roles",
            detail: "a model must serve at least one role".to_owned(),
        });
    }
    Ok(out)
}

pub(super) fn decode_modalities(value: &JsonValue) -> Result<Vec<InputModality>, DefectReason> {
    let items = required(value, "input_modalities")?
        .as_array()
        .ok_or(DefectReason::WrongType {
            field: "input_modalities",
        })?;
    let mut out = Vec::new();
    for item in items {
        let text = item.as_str().ok_or(DefectReason::WrongType {
            field: "input_modalities",
        })?;
        let modality = match text {
            "TEXT" => InputModality::Text,
            "IMAGE" => InputModality::Image,
            other => {
                return Err(DefectReason::UnknownEnumValue {
                    field: "input_modalities",
                    value: other.to_owned(),
                })
            }
        };
        if !out.contains(&modality) {
            out.push(modality);
        }
    }
    if out.is_empty() {
        return Err(DefectReason::Invalid {
            field: "input_modalities",
            detail: "a model must accept at least one".to_owned(),
        });
    }
    Ok(out)
}

pub(super) fn decode_thinking_levels(value: &JsonValue) -> Result<ThinkingLevels, DefectReason> {
    let Some(raw) = value.field("thinking_levels") else {
        return Ok(ThinkingLevels::adapter_defaults());
    };
    let object = raw.as_object().ok_or(DefectReason::WrongType {
        field: "thinking_levels",
    })?;
    let mut entries: Vec<(ThinkingLevel, Option<String>)> = Vec::new();
    for (key, item) in object {
        let level = level_from_wire(key)?;
        let mapped = if item.is_null() {
            None
        } else {
            let text = item.as_str().ok_or(DefectReason::WrongType {
                field: "thinking_levels",
            })?;
            if text.is_empty() || text.len() > MAX_OVERRIDE_LEN {
                return Err(DefectReason::Invalid {
                    field: "thinking_levels",
                    detail: "empty or longer than the accepted bound".to_owned(),
                });
            }
            Some(text.to_owned())
        };
        entries.push((level, mapped));
    }
    Ok(ThinkingLevels::from_entries(entries))
}

pub(super) fn level_from_wire(key: &str) -> Result<ThinkingLevel, DefectReason> {
    LADDER
        .into_iter()
        .find(|level| level_wire_name(*level) == key)
        .ok_or_else(|| DefectReason::UnknownEnumValue {
            field: "thinking_levels",
            value: key.to_owned(),
        })
}

/// The published spelling of a ladder rung.
pub fn level_wire_name(level: ThinkingLevel) -> &'static str {
    match level {
        ThinkingLevel::Off => "OFF",
        ThinkingLevel::Minimal => "MINIMAL",
        ThinkingLevel::Low => "LOW",
        ThinkingLevel::Medium => "MEDIUM",
        ThinkingLevel::High => "HIGH",
        ThinkingLevel::XHigh => "XHIGH",
        ThinkingLevel::Max => "MAX",
    }
}
pub(super) fn decode_price(value: &JsonValue) -> Result<PriceSnapshot, DefectReason> {
    let cost = required(value, "cost")?;
    let currency =
        Currency::new(required_str(cost, "currency")?).map_err(|error| DefectReason::Invalid {
            field: "currency",
            detail: error.to_string(),
        })?;
    let basis = match required_str(cost, "basis")? {
        "METERED" => PriceBasis::Metered,
        "IMPLIED" => PriceBasis::Implied,
        other => {
            return Err(DefectReason::UnknownEnumValue {
                field: "basis",
                value: other.to_owned(),
            })
        }
    };
    Ok(PriceSnapshot {
        snapshot_version: bounded_name(cost, "snapshot_version")?,
        currency,
        basis,
        input_micros_per_million: required_u64(cost, "input_micros_per_million")?,
        output_micros_per_million: required_u64(cost, "output_micros_per_million")?,
        cache_read_micros_per_million: required_u64(cost, "cache_read_micros_per_million")?,
        cache_write_micros_per_million: required_u64(cost, "cache_write_micros_per_million")?,
        long_context_tiers: decode_tiers(cost)?,
    })
}

pub(super) fn decode_tiers(cost: &JsonValue) -> Result<Vec<LongContextTier>, DefectReason> {
    let Some(raw) = cost.field("long_context_tiers") else {
        return Ok(Vec::new());
    };
    let items = raw.as_array().ok_or(DefectReason::WrongType {
        field: "long_context_tiers",
    })?;
    if items.len() > MAX_PRICE_TIERS {
        return Err(DefectReason::Invalid {
            field: "long_context_tiers",
            detail: "more tiers than the accepted bound".to_owned(),
        });
    }
    let mut out = Vec::new();
    for item in items {
        out.push(LongContextTier {
            min_input_tokens: required_u64(item, "min_input_tokens")?,
            input_micros_per_million: required_u64(item, "input_micros_per_million")?,
            output_micros_per_million: required_u64(item, "output_micros_per_million")?,
            cache_read_micros_per_million: required_u64(item, "cache_read_micros_per_million")?,
            cache_write_micros_per_million: required_u64(item, "cache_write_micros_per_million")?,
        });
    }
    Ok(out)
}

pub(super) fn decode_model(value: &JsonValue) -> Result<Model, DefectReason> {
    let schema_version = schema_version(value)?;
    let model_id =
        ModelId::new(required_str(value, "model_id")?).map_err(|error| DefectReason::Invalid {
            field: "model_id",
            detail: error.to_string(),
        })?;
    let provider_id = ProviderId::new(required_str(value, "provider_id")?).map_err(|error| {
        DefectReason::Invalid {
            field: "provider_id",
            detail: error.to_string(),
        }
    })?;
    let wire = match value.field("wire_api") {
        None => None,
        Some(raw) => {
            let text = raw
                .as_str()
                .ok_or(DefectReason::WrongType { field: "wire_api" })?;
            Some(wire_api(text, "wire_api")?)
        }
    };
    let endpoint = match value.field("endpoint") {
        None => None,
        Some(raw) => {
            let text = raw
                .as_str()
                .ok_or(DefectReason::WrongType { field: "endpoint" })?;
            Some(Endpoint::new(text).map_err(|error| DefectReason::Invalid {
                field: "endpoint",
                detail: error.to_string(),
            })?)
        }
    };
    Ok(Model {
        model_id,
        provider_id,
        wire_api: wire,
        endpoint,
        display_name: bounded_name(value, "display_name")?,
        roles: decode_roles(value)?,
        input_modalities: decode_modalities(value)?,
        reasoning: required_bool(value, "reasoning")?,
        // Required, never defaulted: see `Model::tool_calling`. A record that
        // omits it drops here with `MissingField`, which names the entry and
        // the field, rather than routing on an answer nobody gave.
        tool_calling: required_bool(value, "tool_calling")?,
        thinking_levels: decode_thinking_levels(value)?,
        context_window: required_u64(value, "context_window")?,
        max_output_tokens: required_u64(value, "max_output_tokens")?,
        cost: decode_price(value)?,
        compat: string_map(value, "compat", MAX_OVERRIDES)?,
        sampling_defaults: string_map(value, "sampling_defaults", MAX_OVERRIDES)?,
        enabled: required_bool(value, "enabled")?,
        schema_version,
    })
}
