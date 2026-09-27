// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Strict scalar, identity, and opaque-operand decoding.

use bip_types::identity::{SemanticNodeId, TabId};

use super::super::super::{
    ActionIntentCodecError, DomQueryRole, OpaqueOperandKind, OpaqueOperandRef, ScrollDirection,
    MAX_OPAQUE_OPERAND_HANDLE_BYTES,
};
use super::fields;
use crate::tool::{IdempotencyClass, ToolRuntime};
use crate::BrowserSessionId;

pub(super) fn opaque(
    value: &[u8],
    expected: OpaqueOperandKind,
) -> Result<OpaqueOperandRef, ActionIntentCodecError> {
    let parsed = fields::parse(value)?;
    let [handle, kind, digest] = fields::exact(&parsed, [0, 1, 2])?;
    let handle = fields::text(handle, MAX_OPAQUE_OPERAND_HANDLE_BYTES)?;
    let kind = opaque_kind(fields::byte(kind)?)?;
    if kind != expected || digest.len() != 32 || !valid_opaque_handle(&handle, kind) {
        return Err(ActionIntentCodecError::InvalidValue);
    }
    let mut copied = [0_u8; 32];
    copied.copy_from_slice(digest);
    Ok(OpaqueOperandRef {
        handle,
        kind,
        digest: copied,
    })
}

pub(super) fn optional_opaque(
    value: &[u8],
    kind: OpaqueOperandKind,
) -> Result<Option<OpaqueOperandRef>, ActionIntentCodecError> {
    match value {
        [0] => Ok(None),
        [1, nested @ ..] => {
            let parsed = fields::parse(nested)?;
            let [opaque_value] = fields::exact(&parsed, [0])?;
            opaque(opaque_value, kind).map(Some)
        }
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

fn valid_opaque_handle(handle: &str, kind: OpaqueOperandKind) -> bool {
    let Some(rest) = handle.strip_prefix("turn-") else {
        return false;
    };
    let suffix = format!("-{}", kind.label());
    let Some(numbers) = rest.strip_suffix(&suffix) else {
        return false;
    };
    let Some((ordinal, sequence)) = numbers.split_once("-call-") else {
        return false;
    };
    let (Ok(parsed_ordinal), Ok(parsed_sequence)) =
        (ordinal.parse::<u64>(), sequence.parse::<u32>())
    else {
        return false;
    };
    ordinal == parsed_ordinal.to_string() && sequence == parsed_sequence.to_string()
}

pub(super) fn tab_id(value: &[u8]) -> Result<TabId, ActionIntentCodecError> {
    fields::id(value).map(TabId::new)
}

pub(super) fn browser_session_id(value: &[u8]) -> Result<BrowserSessionId, ActionIntentCodecError> {
    BrowserSessionId::new(fields::id(value)?).map_err(|_| ActionIntentCodecError::InvalidValue)
}

pub(super) fn node_id(value: &[u8]) -> Result<SemanticNodeId, ActionIntentCodecError> {
    fields::id(value).map(SemanticNodeId::new)
}

pub(super) fn optional_node(
    value: &[u8],
) -> Result<Option<SemanticNodeId>, ActionIntentCodecError> {
    fields::optional_text(value, fields::MAX_ID_BYTES).map(|value| value.map(SemanticNodeId::new))
}

fn opaque_kind(value: u8) -> Result<OpaqueOperandKind, ActionIntentCodecError> {
    match value {
        0 => Ok(OpaqueOperandKind::SearchQuery),
        1 => Ok(OpaqueOperandKind::DomQueryText),
        2 => Ok(OpaqueOperandKind::LibraryQuery),
        3 => Ok(OpaqueOperandKind::MemoryQuery),
        4 => Ok(OpaqueOperandKind::MemoryStatement),
        5 => Ok(OpaqueOperandKind::PythonTitle),
        6 => Ok(OpaqueOperandKind::PythonContent),
        7 => Ok(OpaqueOperandKind::StoreQuery),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn role_value(value: u8) -> Result<DomQueryRole, ActionIntentCodecError> {
    match value {
        0 => Ok(DomQueryRole::Link),
        1 => Ok(DomQueryRole::Button),
        2 => Ok(DomQueryRole::Field),
        3 => Ok(DomQueryRole::Heading),
        4 => Ok(DomQueryRole::List),
        5 => Ok(DomQueryRole::Table),
        6 => Ok(DomQueryRole::Image),
        7 => Ok(DomQueryRole::Region),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn scroll_value(value: u8) -> Result<ScrollDirection, ActionIntentCodecError> {
    match value {
        0 => Ok(ScrollDirection::Up),
        1 => Ok(ScrollDirection::Down),
        2 => Ok(ScrollDirection::ToNode),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn runtime_value(value: u8) -> Result<ToolRuntime, ActionIntentCodecError> {
    match value {
        0 => Ok(ToolRuntime::Media),
        1 => Ok(ToolRuntime::Python),
        2 => Ok(ToolRuntime::LocalModel),
        3 => Ok(ToolRuntime::Wasm),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn idempotency_value(value: u8) -> Result<IdempotencyClass, ActionIntentCodecError> {
    match value {
        0 => Ok(IdempotencyClass::PureRead),
        1 => Ok(IdempotencyClass::IdempotentWrite),
        2 => Ok(IdempotencyClass::ConditionallyIdempotent),
        3 => Ok(IdempotencyClass::Consequential),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}
