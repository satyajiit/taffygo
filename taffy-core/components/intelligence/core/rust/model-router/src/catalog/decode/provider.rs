// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Decoding one provider entry.
//!
//! A provider is where a request goes and how it authenticates. Its own module
//! because its failure modes are its own: an unknown protocol family, an
//! unknown authentication method, or a header block past the bound drops this
//! provider and leaves every other entry in the document intact.

use super::defect::DefectReason;
use super::field::{
    bounded_name, required, required_bool, required_str, schema_version, string_map, wire_api,
};
use super::limits::{MAX_PRESENTATION_FIELDS, MAX_STATIC_HEADERS};
use crate::catalog::types::{Endpoint, ModelSource, Presentation, Provider, WireApi};
use crate::catalog::AuthMethod;
use crate::ids::ProviderId;
use crate::json::JsonValue;
pub(super) fn decode_provider(value: &JsonValue) -> Result<Provider, DefectReason> {
    let schema_version = schema_version(value)?;
    let provider_id = ProviderId::new(required_str(value, "provider_id")?).map_err(|error| {
        DefectReason::Invalid {
            field: "provider_id",
            detail: error.to_string(),
        }
    })?;
    let display_name = bounded_name(value, "display_name")?;
    let wire = wire_api(required_str(value, "wire_api")?, "wire_api")?;
    let default_endpoint =
        Endpoint::new(required_str(value, "default_endpoint")?).map_err(|error| {
            DefectReason::Invalid {
                field: "default_endpoint",
                detail: error.to_string(),
            }
        })?;
    let auth_methods = decode_auth_methods(value)?;
    let model_source = match required_str(value, "model_source")? {
        "STATIC_CATALOG" => ModelSource::StaticCatalog,
        "DYNAMIC_LISTING" => ModelSource::DynamicListing,
        other => {
            return Err(DefectReason::UnknownEnumValue {
                field: "model_source",
                value: other.to_owned(),
            })
        }
    };
    let static_headers = decode_static_headers(value)?;
    let presentation = decode_presentation(value)?;
    let (oauth_wire_api, oauth_endpoint) = decode_oauth_reach(value)?;
    Ok(Provider {
        provider_id,
        display_name,
        wire_api: wire,
        default_endpoint,
        oauth_wire_api,
        oauth_endpoint,
        auth_methods,
        subscription: required_bool(value, "subscription")?,
        static_headers,
        model_source,
        enabled: required_bool(value, "enabled")?,
        presentation,
        schema_version,
    })
}

/// The second way in, for a vendor a subscription reaches differently.
///
/// Both halves are optional and independent: a vendor may move its address
/// without changing its family, or change its family without moving. What is
/// not optional is being well-formed — a family this build does not know, or
/// an address that is not an absolute `https` URL, drops the provider rather
/// than falling back to the key endpoint. Falling back is the one behaviour
/// that must not happen here: it would spend a subscription token at the
/// address the vendor reserves for keys, and the failure would look like a
/// vendor problem rather than a catalog one.
fn decode_oauth_reach(
    value: &JsonValue,
) -> Result<(Option<WireApi>, Option<Endpoint>), DefectReason> {
    let family = match value.field("oauth_wire_api") {
        Some(raw) => Some(wire_api(
            raw.as_str().ok_or(DefectReason::WrongType {
                field: "oauth_wire_api",
            })?,
            "oauth_wire_api",
        )?),
        None => None,
    };
    let endpoint = match value.field("oauth_endpoint") {
        Some(raw) => Some(
            Endpoint::new(raw.as_str().ok_or(DefectReason::WrongType {
                field: "oauth_endpoint",
            })?)
            .map_err(|error| DefectReason::Invalid {
                field: "oauth_endpoint",
                detail: error.to_string(),
            })?,
        ),
        None => None,
    };
    Ok((family, endpoint))
}

/// The optional setup facts, dropped as a whole when the object is malformed.
///
/// Absent is the ordinary case: most vendors say nothing extra, and a row that
/// says nothing must decode to a row that shows nothing. A key inside the
/// object that is not a string, is out of bounds, or is a link that is not
/// `https` is a defect in the row rather than a value to sanitize — this
/// catalog is served, and a link a person is invited to open is one the
/// document does not get to choose the scheme of.
fn decode_presentation(value: &JsonValue) -> Result<Presentation, DefectReason> {
    let entries = string_map(value, "presentation", MAX_PRESENTATION_FIELDS)?;
    let mut presentation = Presentation::default();
    for (key, text) in entries {
        if text.is_empty() {
            return Err(DefectReason::Invalid {
                field: "presentation",
                detail: "a presentation value was empty".to_owned(),
            });
        }
        match key.as_str() {
            "key_prefix" => presentation.key_prefix = Some(text),
            "get_key_url" => presentation.get_key_url = Some(https_link(text)?),
            "docs_url" => presentation.docs_url = Some(https_link(text)?),
            other => {
                return Err(DefectReason::UnknownEnumValue {
                    field: "presentation",
                    value: other.to_owned(),
                })
            }
        }
    }
    Ok(presentation)
}

fn https_link(text: String) -> Result<String, DefectReason> {
    if text.starts_with("https://") && !text.contains(['#', ' ']) {
        return Ok(text);
    }
    Err(DefectReason::Invalid {
        field: "presentation",
        detail: "a presentation link was not an https URL".to_owned(),
    })
}

fn decode_static_headers(
    value: &JsonValue,
) -> Result<std::collections::BTreeMap<String, String>, DefectReason> {
    let headers = string_map(value, "static_headers", MAX_STATIC_HEADERS)?;
    if headers
        .iter()
        .any(|(name, value)| sensitive_header_name(name) || secret_shaped_value(value))
    {
        return Err(DefectReason::Invalid {
            field: "static_headers",
            detail: "a static header looked like a credential".to_owned(),
        });
    }
    Ok(headers)
}

fn sensitive_header_name(name: &str) -> bool {
    let lowercase = name.to_ascii_lowercase();
    let words = lowercase
        .split(['-', '_'])
        .filter(|word| !word.is_empty())
        .collect::<Vec<_>>();
    words.iter().any(|word| {
        matches!(
            *word,
            "authorization" | "cookie" | "token" | "secret" | "password" | "credential"
        ) || *word == "apikey"
    }) || words
        .windows(2)
        .any(|pair| pair.first() == Some(&"api") && pair.get(1) == Some(&"key"))
}

fn secret_shaped_value(value: &str) -> bool {
    let trimmed = value.trim();
    let lowercase = trimmed.to_ascii_lowercase();
    let has_auth_scheme = ["bearer", "basic"].iter().any(|scheme| {
        lowercase
            .strip_prefix(scheme)
            .is_some_and(|tail| tail.starts_with(char::is_whitespace))
    });
    has_auth_scheme
        || [
            "sk-",
            "AIza",
            "ghp_",
            "gho_",
            "ghu_",
            "ghs_",
            "ghr_",
            "github_pat_",
            "xoxb-",
            "xoxa-",
            "xoxp-",
            "xoxr-",
            "xoxs-",
        ]
        .iter()
        .any(|prefix| trimmed.starts_with(prefix))
        || looks_like_jwt(trimmed)
}

fn looks_like_jwt(value: &str) -> bool {
    let mut parts = value.split('.');
    let Some(header) = parts.next() else {
        return false;
    };
    let Some(payload) = parts.next() else {
        return false;
    };
    let Some(signature) = parts.next() else {
        return false;
    };
    parts.next().is_none()
        && header.starts_with("eyJ")
        && [header, payload, signature].iter().all(|part| {
            !part.is_empty()
                && part
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'-'))
        })
}

pub(super) fn decode_auth_methods(value: &JsonValue) -> Result<Vec<AuthMethod>, DefectReason> {
    let items = required(value, "auth_methods")?
        .as_array()
        .ok_or(DefectReason::WrongType {
            field: "auth_methods",
        })?;
    let mut out = Vec::new();
    for item in items {
        let text = item.as_str().ok_or(DefectReason::WrongType {
            field: "auth_methods",
        })?;
        let method = match text {
            "API_KEY" => AuthMethod::ApiKey,
            "OAUTH" => AuthMethod::Oauth,
            other => {
                return Err(DefectReason::UnknownEnumValue {
                    field: "auth_methods",
                    value: other.to_owned(),
                })
            }
        };
        if !out.contains(&method) {
            out.push(method);
        }
    }
    if out.is_empty() {
        return Err(DefectReason::Invalid {
            field: "auth_methods",
            detail: "a provider must declare at least one".to_owned(),
        });
    }
    Ok(out)
}
