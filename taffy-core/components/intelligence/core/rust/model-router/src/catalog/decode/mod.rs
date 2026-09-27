// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fail-closed decoding of a catalog document.
//!
//! The catalog is remote input to a trusted process, so decoding is the
//! security boundary rather than a convenience layer. Two rules shape all of
//! it:
//!
//! - **Granularity is the entry.** An unknown schema version, an unknown value
//!   in a closed enumeration, a missing kill switch, or a malformed key drops
//!   that provider or model and records a defect. It never fails the load and
//!   it never crashes: a served catalog must not be able to take model access
//!   away wholesale.
//! - **Everything is bounded.** Entry counts, name lengths, header counts, and
//!   dialect-override sizes all have limits, checked before anything is stored.
//!
//! A dropped entry is not a silent one. Every defect carries where it was and
//! why, so a bad publish is visible as configuration state rather than as a
//! model that quietly stopped being offered.
//!
//! # How this module is laid out
//!
//! | Module | Owns |
//! |---|---|
//! | [`limits`] | Every bound, in one place |
//! | [`defect`] | What was dropped and why |
//! | [`field`] | Reading one untrusted field, fail-closed |
//! | [`provider`] | One provider entry |
//! | [`model`] | One model entry and its four enumerations |
//!
//! What is left here is the document walk itself: header, providers, models.

mod defect;
mod field;
mod limits;
mod model;
mod provider;

use std::collections::BTreeSet;

pub use self::defect::{CatalogDefect, DefectLocation, DefectReason, ParsedCatalog};
pub use self::limits::{
    MAX_DISPLAY_NAME_LEN, MAX_MODELS, MAX_OVERRIDES, MAX_OVERRIDE_LEN, MAX_PRICE_TIERS,
    MAX_PROVIDERS, MAX_STATIC_HEADERS,
};
pub use self::model::level_wire_name;

use self::model::decode_model;
use self::provider::decode_provider;
use crate::catalog::types::{
    CatalogDocument, CatalogHeader, Model, Provider, SUPPORTED_SCHEMA_VERSION,
};
use crate::ids::ModelKey;
use crate::json::{self, JsonError, JsonValue};
use crate::time::Timestamp;

/// Decodes a catalog document.
///
/// A malformed document is a hard error: there is nothing to salvage from
/// bytes that are not JSON. A well-formed document with bad entries decodes to
/// the entries that are good and a defect for each that is not.
pub fn parse_document(text: &str) -> Result<ParsedCatalog, JsonError> {
    let root = json::parse(text)?;
    let mut defects = Vec::new();
    let header = decode_header(&root, &mut defects);
    let providers = decode_providers(&root, &mut defects);
    let models = decode_models(&root, &mut defects);
    Ok(ParsedCatalog {
        document: CatalogDocument {
            header,
            providers,
            models,
        },
        defects,
    })
}

fn decode_header(root: &JsonValue, defects: &mut Vec<CatalogDefect>) -> CatalogHeader {
    let mut push = |reason: DefectReason| {
        defects.push(CatalogDefect {
            location: DefectLocation::Document,
            reason,
        });
    };
    let schema_version = root
        .field("schema_version")
        .and_then(JsonValue::as_i64)
        .and_then(|value| u32::try_from(value).ok());
    let schema_version = match schema_version {
        Some(value) if value == SUPPORTED_SCHEMA_VERSION => value,
        Some(declared) => {
            push(DefectReason::UnsupportedSchemaVersion { declared });
            declared
        }
        None => {
            push(DefectReason::MissingField {
                field: "schema_version",
            });
            0
        }
    };
    let catalog_version = root
        .field("catalog_version")
        .and_then(JsonValue::as_str)
        .unwrap_or_default()
        .to_owned();
    if catalog_version.is_empty() {
        push(DefectReason::MissingField {
            field: "catalog_version",
        });
    }
    let generated_at = root
        .field("generated_at")
        .and_then(JsonValue::as_str)
        .and_then(|text| Timestamp::new(text).ok());
    let generated_at = generated_at.unwrap_or_else(|| {
        push(DefectReason::Invalid {
            field: "generated_at",
            detail: "not a canonical UTC timestamp".to_owned(),
        });
        Timestamp::floor()
    });
    CatalogHeader {
        schema_version,
        catalog_version,
        generated_at,
    }
}

fn decode_providers(root: &JsonValue, defects: &mut Vec<CatalogDefect>) -> Vec<Provider> {
    let Some(items) = root.field("providers").and_then(JsonValue::as_array) else {
        defects.push(CatalogDefect {
            location: DefectLocation::Document,
            reason: DefectReason::MissingField { field: "providers" },
        });
        return Vec::new();
    };
    let mut out: Vec<Provider> = Vec::new();
    let mut identities = BTreeSet::new();
    for (index, item) in items.iter().enumerate() {
        let location = DefectLocation::Provider {
            index,
            provider_id: item
                .field("provider_id")
                .and_then(JsonValue::as_str)
                .map(str::to_owned),
        };
        if out.len() >= MAX_PROVIDERS {
            defects.push(CatalogDefect {
                location,
                reason: DefectReason::TooManyEntries,
            });
            continue;
        }
        match decode_provider(item) {
            Ok(provider) => {
                if identities.insert(provider.provider_id.clone()) {
                    out.push(provider);
                } else {
                    defects.push(CatalogDefect {
                        location,
                        reason: DefectReason::DuplicateEntry,
                    });
                }
            }
            Err(reason) => defects.push(CatalogDefect { location, reason }),
        }
    }
    out
}

fn decode_models(root: &JsonValue, defects: &mut Vec<CatalogDefect>) -> Vec<Model> {
    let Some(items) = root.field("models").and_then(JsonValue::as_array) else {
        defects.push(CatalogDefect {
            location: DefectLocation::Document,
            reason: DefectReason::MissingField { field: "models" },
        });
        return Vec::new();
    };
    let mut out: Vec<Model> = Vec::new();
    let mut identities = BTreeSet::new();
    for (index, item) in items.iter().enumerate() {
        let location = DefectLocation::Model {
            index,
            model_id: item
                .field("model_id")
                .and_then(JsonValue::as_str)
                .map(str::to_owned),
        };
        if out.len() >= MAX_MODELS {
            defects.push(CatalogDefect {
                location,
                reason: DefectReason::TooManyEntries,
            });
            continue;
        }
        match decode_model(item) {
            Ok(model) => {
                let identity = ModelKey::new(model.provider_id.clone(), model.model_id.clone());
                if identities.insert(identity) {
                    out.push(model);
                } else {
                    defects.push(CatalogDefect {
                        location,
                        reason: DefectReason::DuplicateEntry,
                    });
                }
            }
            Err(reason) => defects.push(CatalogDefect { location, reason }),
        }
    }
    out
}
