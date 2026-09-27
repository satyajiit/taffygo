// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact field readers for one canonical managed frame.

use std::collections::BTreeMap;

use crate::cost::TokenUsage;
use crate::json::{self, JsonValue};
use crate::request::{ErrorClass, StopReason};

use super::ManagedReplyDefect;
use crate::wire::managed::MANAGED_SCHEMA_VERSION;

pub(super) fn read_header<'a>(
    document: &'a JsonValue,
    expected_request_id: &str,
) -> Result<(&'a BTreeMap<String, JsonValue>, &'a str), ManagedReplyDefect> {
    let object = document
        .as_object()
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    let version = object
        .get("schema_version")
        .and_then(JsonValue::as_i64)
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    if version != i64::try_from(MANAGED_SCHEMA_VERSION).unwrap_or(i64::MAX) {
        return Err(ManagedReplyDefect::UnsupportedSchemaVersion);
    }
    let request_id = object
        .get("request_id")
        .and_then(JsonValue::as_str)
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    if !request_id.eq_ignore_ascii_case(expected_request_id) {
        return Err(ManagedReplyDefect::RequestMismatch);
    }
    let event_type = object
        .get("type")
        .and_then(JsonValue::as_str)
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    Ok((object, event_type))
}

pub(super) fn read_route(value: Option<&JsonValue>) -> Result<(), ManagedReplyDefect> {
    let route = value
        .and_then(JsonValue::as_object)
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    if !exact_fields(
        route,
        &[
            "mode",
            "provider_id",
            "model_id",
            "wire_api",
            "attempts",
            "failover",
            "price_basis",
        ],
    ) || route.get("mode").and_then(JsonValue::as_str) != Some("managed")
        || !matches!(
            route.get("price_basis").and_then(JsonValue::as_str),
            Some("METERED" | "IMPLIED")
        )
        || nonempty_bounded(route.get("provider_id")).is_err()
        || nonempty_bounded(route.get("model_id")).is_err()
        || nonempty_bounded(route.get("wire_api")).is_err()
    {
        return Err(ManagedReplyDefect::MalformedFrame);
    }
    unsigned(route.get("attempts"))?;
    unsigned(route.get("failover"))?;
    Ok(())
}

pub(super) fn read_usage(value: Option<&JsonValue>) -> Result<TokenUsage, ManagedReplyDefect> {
    let usage = value
        .and_then(JsonValue::as_object)
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    if !exact_fields(
        usage,
        &[
            "input_tokens",
            "output_tokens",
            "cache_read_tokens",
            "cache_write_tokens",
        ],
    ) {
        return Err(ManagedReplyDefect::MalformedFrame);
    }
    Ok(TokenUsage {
        input: unsigned(usage.get("input_tokens"))?,
        output: unsigned(usage.get("output_tokens"))?,
        cache_read: unsigned(usage.get("cache_read_tokens"))?,
        cache_write: unsigned(usage.get("cache_write_tokens"))?,
    })
}

pub(super) fn read_cost(value: Option<&JsonValue>) -> Result<(), ManagedReplyDefect> {
    let cost = value
        .and_then(JsonValue::as_object)
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    if !exact_fields(cost, &["currency", "micro_usd", "credits"])
        || cost.get("currency").and_then(JsonValue::as_str) != Some("USD")
    {
        return Err(ManagedReplyDefect::MalformedFrame);
    }
    unsigned(cost.get("micro_usd"))?;
    unsigned(cost.get("credits"))?;
    Ok(())
}

pub(super) fn read_stop(value: Option<&JsonValue>) -> Result<StopReason, ManagedReplyDefect> {
    match value.and_then(JsonValue::as_str) {
        Some("end_turn") => Ok(StopReason::Complete),
        Some("max_tokens") => Ok(StopReason::Length),
        Some("tool_call") => Ok(StopReason::ToolCall),
        Some("stop_sequence" | "other") => Ok(StopReason::ProviderStop),
        _ => Err(ManagedReplyDefect::MalformedFrame),
    }
}

pub(super) fn read_error_class(
    value: Option<&JsonValue>,
) -> Result<ErrorClass, ManagedReplyDefect> {
    match value.and_then(JsonValue::as_str) {
        Some("auth") => Ok(ErrorClass::Auth),
        Some("quota") => Ok(ErrorClass::Quota),
        Some("overloaded") => Ok(ErrorClass::Overloaded),
        Some("invalid_request") => Ok(ErrorClass::InvalidRequest),
        Some("network") => Ok(ErrorClass::Network),
        Some("overflow") => Ok(ErrorClass::Overflow),
        Some("canceled") => Ok(ErrorClass::Canceled),
        Some("unknown") => Ok(ErrorClass::Unknown),
        _ => Err(ManagedReplyDefect::MalformedFrame),
    }
}

pub(super) fn unsigned(value: Option<&JsonValue>) -> Result<u64, ManagedReplyDefect> {
    let value = value
        .and_then(JsonValue::as_i64)
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    u64::try_from(value).map_err(|_| ManagedReplyDefect::MalformedFrame)
}

pub(super) fn nonempty_bounded(value: Option<&JsonValue>) -> Result<&str, ManagedReplyDefect> {
    let text = value
        .and_then(JsonValue::as_str)
        .ok_or(ManagedReplyDefect::MalformedFrame)?;
    if text.is_empty() || text.len() > json::MAX_INPUT_BYTES {
        return Err(ManagedReplyDefect::MalformedFrame);
    }
    Ok(text)
}

pub(super) fn exact_fields(object: &BTreeMap<String, JsonValue>, fields: &[&str]) -> bool {
    object.len() == fields.len() && fields.iter().all(|field| object.contains_key(*field))
}
