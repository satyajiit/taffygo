// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable Library, Memory, store, and isolated-tool intent decoders.

use super::super::super::{
    ActionIntent, ActionIntentCodecError, LibraryIntent, MediaOperation, MediaToolIntent,
    MemoryIntent, MemoryScopeIntent, OpaqueOperandKind, PythonEntrypoint, StoreIntent,
    ToolJobIntent, MAX_LIBRARY_SEARCH_RESULTS, MAX_MEMORY_SEARCH_RESULTS, MAX_STORE_RESULTS,
};
use super::fields;
use super::values::{idempotency_value, opaque, optional_node, runtime_value, tab_id};

pub(super) fn decode_store(
    parsed: &[fields::Field<'_>],
) -> Result<StoreIntent, ActionIntentCodecError> {
    let kind = parsed
        .get(1)
        .filter(|field| field.tag == 1)
        .ok_or(ActionIntentCodecError::MissingField)
        .and_then(|field| fields::byte(field.value))?;
    match kind {
        0 | 2 => {
            let [_, _, tab, query, limit] = fields::exact(parsed, [0, 1, 2, 3, 4])?;
            let limit = store_limit(limit)?;
            let tab = tab_id(tab)?;
            let query = opaque(query, OpaqueOperandKind::StoreQuery)?;
            Ok(if kind == 0 {
                StoreIntent::HistorySearch { tab, query, limit }
            } else {
                StoreIntent::BookmarksSearch { tab, query, limit }
            })
        }
        1 | 3 => {
            let [_, _, tab, limit] = fields::exact(parsed, [0, 1, 2, 3])?;
            let limit = store_limit(limit)?;
            let tab = tab_id(tab)?;
            Ok(if kind == 1 {
                StoreIntent::HistoryRecent { tab, limit }
            } else {
                StoreIntent::BookmarksList { tab, limit }
            })
        }
        4 => {
            let [_, _, tab] = fields::exact(parsed, [0, 1, 2])?;
            Ok(StoreIntent::OpenTabsList { tab: tab_id(tab)? })
        }
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

fn store_limit(field: &[u8]) -> Result<u32, ActionIntentCodecError> {
    let limit = fields::u32_value(field)?;
    if limit == 0 || limit > MAX_STORE_RESULTS {
        return Err(ActionIntentCodecError::InvalidValue);
    }
    Ok(limit)
}

pub(super) fn decode_memory(
    parsed: &[fields::Field<'_>],
) -> Result<MemoryIntent, ActionIntentCodecError> {
    let kind = parsed
        .get(1)
        .filter(|field| field.tag == 1)
        .ok_or(ActionIntentCodecError::MissingField)
        .and_then(|field| fields::byte(field.value))?;
    match kind {
        0 => {
            let [_, _, tab, query, limit] = fields::exact(parsed, [0, 1, 2, 3, 4])?;
            let limit = fields::u32_value(limit)?;
            if limit == 0 || limit > MAX_MEMORY_SEARCH_RESULTS {
                return Err(ActionIntentCodecError::InvalidValue);
            }
            Ok(MemoryIntent::Search {
                tab: tab_id(tab)?,
                query: opaque(query, OpaqueOperandKind::MemoryQuery)?,
                limit,
            })
        }
        1 => {
            let [_, _, tab, statement, scope, workspace, expiry] =
                fields::exact(parsed, [0, 1, 2, 3, 4, 5, 6])?;
            Ok(MemoryIntent::Save {
                tab: tab_id(tab)?,
                statement: opaque(statement, OpaqueOperandKind::MemoryStatement)?,
                scope: memory_scope(scope, workspace)?,
                expires_at_epoch_ms: optional_nonzero_u64(expiry)?,
            })
        }
        2 => {
            let [_, _, tab, memory, revision, statement, scope, workspace, expiry] =
                fields::exact(parsed, [0, 1, 2, 3, 4, 5, 6, 7, 8])?;
            let record_revision = fields::u64_value(revision)?;
            if record_revision == 0 {
                return Err(ActionIntentCodecError::InvalidValue);
            }
            Ok(MemoryIntent::Update {
                tab: tab_id(tab)?,
                memory_id: fields::id(memory)?,
                record_revision,
                statement: opaque(statement, OpaqueOperandKind::MemoryStatement)?,
                scope: memory_scope(scope, workspace)?,
                expires_at_epoch_ms: optional_nonzero_u64(expiry)?,
            })
        }
        3 => {
            let [_, _, tab, memory, revision] = fields::exact(parsed, [0, 1, 2, 3, 4])?;
            let record_revision = fields::u64_value(revision)?;
            if record_revision == 0 {
                return Err(ActionIntentCodecError::InvalidValue);
            }
            Ok(MemoryIntent::Delete {
                tab: tab_id(tab)?,
                memory_id: fields::id(memory)?,
                record_revision,
            })
        }
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

fn memory_scope(
    kind: &[u8],
    workspace: &[u8],
) -> Result<MemoryScopeIntent, ActionIntentCodecError> {
    let workspace = fields::optional_text(workspace, fields::MAX_ID_BYTES)?;
    match (fields::byte(kind)?, workspace) {
        (0, None) => Ok(MemoryScopeIntent::AllTasks),
        (1, Some(value)) => Ok(MemoryScopeIntent::Workspace {
            workspace_id: fields::id(value.as_bytes())?,
        }),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

fn optional_nonzero_u64(field: &[u8]) -> Result<Option<u64>, ActionIntentCodecError> {
    let value = fields::optional_u64(field)?;
    if value == Some(0) {
        return Err(ActionIntentCodecError::InvalidValue);
    }
    Ok(value)
}

pub(super) fn decode_library(
    parsed: &[fields::Field<'_>],
) -> Result<LibraryIntent, ActionIntentCodecError> {
    let kind = parsed
        .get(1)
        .filter(|field| field.tag == 1)
        .ok_or(ActionIntentCodecError::MissingField)
        .and_then(|field| fields::byte(field.value))?;
    match kind {
        0 => {
            let [_, _, tab, query, limit] = fields::exact(parsed, [0, 1, 2, 3, 4])?;
            let limit = fields::u32_value(limit)?;
            if limit == 0 || limit > MAX_LIBRARY_SEARCH_RESULTS {
                return Err(ActionIntentCodecError::InvalidValue);
            }
            Ok(LibraryIntent::Search {
                tab: tab_id(tab)?,
                query: opaque(query, OpaqueOperandKind::LibraryQuery)?,
                limit,
            })
        }
        1 => {
            let [_, _, tab, workspace, workspace_revision, fact, entry_revision] =
                fields::exact(parsed, [0, 1, 2, 3, 4, 5, 6])?;
            let workspace_revision = fields::u64_value(workspace_revision)?;
            if workspace_revision == 0 {
                return Err(ActionIntentCodecError::InvalidValue);
            }
            Ok(LibraryIntent::Save {
                tab: tab_id(tab)?,
                workspace_id: fields::id(workspace)?,
                workspace_revision,
                fact_id: fields::id(fact)?,
                entry_revision: fields::u64_value(entry_revision)?,
            })
        }
        2 => {
            let [_, _, tab, entry, entry_revision] = fields::exact(parsed, [0, 1, 2, 3, 4])?;
            let entry_revision = fields::u64_value(entry_revision)?;
            if entry_revision == 0 {
                return Err(ActionIntentCodecError::InvalidValue);
            }
            Ok(LibraryIntent::Remove {
                tab: tab_id(tab)?,
                entry_id: fields::id(entry)?,
                entry_revision,
            })
        }
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn decode_tool_job(
    parsed: &[fields::Field<'_>],
) -> Result<ActionIntent, ActionIntentCodecError> {
    let [_, name, runtime, tab, node, idempotency, entrypoint, title, content] =
        fields::exact(parsed, [0, 1, 2, 3, 4, 5, 6, 7, 8])?;
    Ok(ActionIntent::ToolJob(ToolJobIntent {
        tool_name: fields::tool_name(name)?,
        runtime: runtime_value(fields::byte(runtime)?)?,
        entrypoint: PythonEntrypoint::from_wire(fields::byte(entrypoint)?)
            .ok_or(ActionIntentCodecError::InvalidValue)?,
        title: opaque(title, OpaqueOperandKind::PythonTitle)?,
        content: opaque(content, OpaqueOperandKind::PythonContent)?,
        tab: tab_id(tab)?,
        node: optional_node(node)?,
        idempotency: idempotency_value(fields::byte(idempotency)?)?,
    }))
}

pub(super) fn decode_media_tool(
    parsed: &[fields::Field<'_>],
) -> Result<ActionIntent, ActionIntentCodecError> {
    let [_, name, operation, source, browser_session, tab, node, max_frames, idempotency, source_bytes] =
        fields::exact(parsed, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9])?;
    let tool_name = fields::tool_name(name)?;
    let operation = MediaOperation::from_wire(fields::byte(operation)?)
        .ok_or(ActionIntentCodecError::InvalidValue)?;
    let max_frames = fields::u32_value(max_frames)?;
    let source_bytes = fields::u64_value(source_bytes)?;
    if tool_name != operation.tool_name()
        || !(1..=16 * 1024 * 1024).contains(&source_bytes)
        || (operation == MediaOperation::SampleFrames && !(1..=12).contains(&max_frames))
        || (operation != MediaOperation::SampleFrames && max_frames != 0)
    {
        return Err(ActionIntentCodecError::InvalidValue);
    }
    Ok(ActionIntent::MediaTool(MediaToolIntent {
        tool_name,
        operation,
        source_id: fields::id(source)?,
        source_browser_session_id: super::values::browser_session_id(browser_session)?,
        source_bytes,
        tab: tab_id(tab)?,
        node: optional_node(node)?,
        max_frames,
        idempotency: idempotency_value(fields::byte(idempotency)?)?,
    }))
}
