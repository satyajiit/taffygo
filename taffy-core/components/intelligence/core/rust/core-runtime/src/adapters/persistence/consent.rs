// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable conversion for browser-resolved initial-consent sources.

use bip_types::identity::TabId;
use core_service_types as wire;
use task_engine::{ConsentedSource, SourceId};

use super::ConversionError;

pub(super) fn sources(values: &[ConsentedSource]) -> Vec<wire::PersistedConsentedSource> {
    values.iter().map(source).collect()
}

pub(super) fn source(value: &ConsentedSource) -> wire::PersistedConsentedSource {
    wire::PersistedConsentedSource {
        source_id: value.source_id.to_text(),
        tab_id: value.tab_id.0.clone(),
        normalized_origin: value.normalized_origin.clone(),
        canonical_locator: value.canonical_locator.clone(),
    }
}

pub(super) fn unsources(
    values: Vec<wire::PersistedConsentedSource>,
) -> Result<Vec<ConsentedSource>, ConversionError> {
    if values.len() > wire::MAX_TASK_CONSENT_SOURCES {
        return Err(ConversionError::InvalidValue);
    }
    let mut result = Vec::with_capacity(values.len());
    for value in values {
        let restored = unsource(value)?;
        if result
            .last()
            .is_some_and(|previous: &ConsentedSource| previous.source_id >= restored.source_id)
        {
            return Err(ConversionError::InvalidValue);
        }
        result.push(restored);
    }
    Ok(result)
}

pub(super) fn unsource(
    value: wire::PersistedConsentedSource,
) -> Result<ConsentedSource, ConversionError> {
    if !valid_tab_id(&value.tab_id)
        || value.normalized_origin.len() > wire::MAX_NORMALIZED_ORIGIN_BYTES
    {
        return Err(ConversionError::InvalidValue);
    }
    let source_id =
        SourceId::parse(&value.source_id).map_err(|_| ConversionError::InvalidIdentifier)?;
    let origin = policy_engine::origin::normalize_serialization(&value.normalized_origin)
        .map_err(|_| ConversionError::InvalidValue)?;
    if origin.display() != value.normalized_origin {
        return Err(ConversionError::InvalidValue);
    }
    Ok(ConsentedSource {
        source_id,
        tab_id: TabId(value.tab_id),
        normalized_origin: value.normalized_origin,
        canonical_locator: value.canonical_locator,
    })
}

pub(super) fn valid_tab_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= wire::MAX_IDENTIFIER_BYTES
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'))
}
