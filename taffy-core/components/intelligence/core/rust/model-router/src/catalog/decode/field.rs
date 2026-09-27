// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reading one field of untrusted JSON, once.
//!
//! Every accessor here is total and fail-closed: it returns a
//! [`DefectReason`] rather than a default, and it applies the bound from
//! [`super::limits`] before anything is stored. Nothing in this module knows
//! what a provider or a model is — that is exactly why it can be trusted by
//! both.

use std::collections::BTreeMap;

use super::defect::DefectReason;
use super::limits::{MAX_DISPLAY_NAME_LEN, MAX_OVERRIDE_LEN};
use crate::catalog::types::{WireApi, SUPPORTED_SCHEMA_VERSION};
use crate::json::JsonValue;
pub(super) fn required<'a>(
    value: &'a JsonValue,
    field: &'static str,
) -> Result<&'a JsonValue, DefectReason> {
    value
        .field(field)
        .ok_or(DefectReason::MissingField { field })
}

pub(super) fn required_str<'a>(
    value: &'a JsonValue,
    field: &'static str,
) -> Result<&'a str, DefectReason> {
    required(value, field)?
        .as_str()
        .ok_or(DefectReason::WrongType { field })
}

pub(super) fn required_bool(value: &JsonValue, field: &'static str) -> Result<bool, DefectReason> {
    required(value, field)?
        .as_bool()
        .ok_or(DefectReason::WrongType { field })
}

pub(super) fn required_u64(value: &JsonValue, field: &'static str) -> Result<u64, DefectReason> {
    let raw = required(value, field)?
        .as_i64()
        .ok_or(DefectReason::WrongType { field })?;
    u64::try_from(raw).map_err(|_| DefectReason::Invalid {
        field,
        detail: "negative".to_owned(),
    })
}

pub(super) fn bounded_name(value: &JsonValue, field: &'static str) -> Result<String, DefectReason> {
    let text = required_str(value, field)?;
    if text.is_empty() || text.len() > MAX_DISPLAY_NAME_LEN {
        return Err(DefectReason::Invalid {
            field,
            detail: "empty or longer than the accepted bound".to_owned(),
        });
    }
    Ok(text.to_owned())
}

pub(super) fn schema_version(value: &JsonValue) -> Result<u32, DefectReason> {
    let declared = required_u64(value, "schema_version")?;
    let declared = u32::try_from(declared).unwrap_or(u32::MAX);
    if declared == SUPPORTED_SCHEMA_VERSION {
        Ok(declared)
    } else {
        Err(DefectReason::UnsupportedSchemaVersion { declared })
    }
}

pub(super) fn wire_api(text: &str, field: &'static str) -> Result<WireApi, DefectReason> {
    match text {
        "ANTHROPIC_MESSAGES" => Ok(WireApi::AnthropicMessages),
        "OPEN_AI_RESPONSES" => Ok(WireApi::OpenAiResponses),
        "OPEN_AI_COMPLETIONS" => Ok(WireApi::OpenAiCompletions),
        "GOOGLE_GENERATIVE_LANGUAGE" => Ok(WireApi::GoogleGenerativeLanguage),
        "OPEN_AI_CODEX_RESPONSES" => Ok(WireApi::OpenAiCodexResponses),
        "GOOGLE_CLOUD_CODE_ASSIST" => Ok(WireApi::GoogleCloudCodeAssist),
        other => Err(DefectReason::UnknownEnumValue {
            field,
            value: other.to_owned(),
        }),
    }
}

pub(super) fn string_map(
    value: &JsonValue,
    field: &'static str,
    limit: usize,
) -> Result<BTreeMap<String, String>, DefectReason> {
    let Some(raw) = value.field(field) else {
        return Ok(BTreeMap::new());
    };
    let object = raw.as_object().ok_or(DefectReason::WrongType { field })?;
    if object.len() > limit {
        return Err(DefectReason::Invalid {
            field,
            detail: "more entries than the accepted bound".to_owned(),
        });
    }
    let mut out = BTreeMap::new();
    for (key, item) in object {
        let text = item.as_str().ok_or(DefectReason::WrongType { field })?;
        if key.len() > MAX_OVERRIDE_LEN || text.len() > MAX_OVERRIDE_LEN {
            return Err(DefectReason::Invalid {
                field,
                detail: "key or value longer than the accepted bound".to_owned(),
            });
        }
        out.insert(key.clone(), text.to_owned());
    }
    Ok(out)
}
