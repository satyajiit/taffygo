// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{
    MemoryRecord, MemoryScope, MemorySource, MemoryWorkspace, MAX_MEMORY_OPERATION_ID_BYTES,
    MAX_MEMORY_STATEMENT_BYTES, MAX_MEMORY_TASK_ID_BYTES,
};
use crate::workspace::MAX_DISPLAY_NAME_BYTES;

pub(super) fn valid_record(record: &MemoryRecord) -> bool {
    record.revision > 0
        && valid_text(&record.statement, MAX_MEMORY_STATEMENT_BYTES)
        && valid_source(&record.source)
        && valid_scope(&record.scope)
        && record.updated_at_epoch_ms >= record.created_at_epoch_ms
        && record.reviewed_at_epoch_ms.is_none_or(|reviewed| {
            reviewed >= record.created_at_epoch_ms && reviewed <= record.updated_at_epoch_ms
        })
        && record
            .expires_at_epoch_ms
            .is_none_or(|expiry| expiry > record.updated_at_epoch_ms)
        && match record.source {
            MemorySource::UserEntered => true,
            MemorySource::AcceptedTaskSuggestion { .. } => record.reviewed_at_epoch_ms.is_some(),
        }
}

pub(super) fn valid_operation_id(value: &str) -> bool {
    valid_text(value, MAX_MEMORY_OPERATION_ID_BYTES)
}

fn valid_source(source: &MemorySource) -> bool {
    match source {
        MemorySource::UserEntered => true,
        MemorySource::AcceptedTaskSuggestion { task_id, workspace } => {
            valid_text(task_id, MAX_MEMORY_TASK_ID_BYTES)
                && workspace.as_ref().is_none_or(valid_workspace)
        }
    }
}

fn valid_scope(scope: &MemoryScope) -> bool {
    match scope {
        MemoryScope::AllTasks => true,
        MemoryScope::Workspace(workspace) => valid_workspace(workspace),
    }
}

fn valid_workspace(workspace: &MemoryWorkspace) -> bool {
    valid_text(&workspace.display_name, MAX_DISPLAY_NAME_BYTES)
}

fn valid_text(value: &str, maximum: usize) -> bool {
    !value.is_empty()
        && value.len() <= maximum
        && value.trim() == value
        && !value.chars().any(char::is_control)
}
