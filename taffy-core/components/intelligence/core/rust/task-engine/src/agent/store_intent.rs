// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projection of validated store calls into exact durable intents.

use crate::action::{
    ActionIntent, OpaqueOperandKind, StoreIntent, DEFAULT_STORE_RESULTS, MAX_STORE_RESULTS,
};
use crate::tool::ArgumentValue;
use crate::workflow::WorkflowDigest;

use super::target::CallTarget;
use super::{AgentError, ModelToolCall, TurnResidency};

pub(super) fn store_intent(
    call: &ModelToolCall,
    turn_ordinal: u64,
    sequence: u32,
    residency: &TurnResidency,
    target: &CallTarget,
    digest: &dyn WorkflowDigest,
) -> Result<ActionIntent, AgentError> {
    let tab = target.tab_id.clone();
    let limit = || -> Result<u32, AgentError> {
        let limit = optional_count(call, "limit")
            .map(u32::try_from)
            .transpose()
            .map_err(|_| AgentError::ContradictoryReply)?
            .unwrap_or(DEFAULT_STORE_RESULTS);
        if limit == 0 || limit > MAX_STORE_RESULTS {
            return Err(AgentError::ContradictoryReply);
        }
        Ok(limit)
    };
    let query = || {
        residency
            .action_operands()
            .bind(
                turn_ordinal,
                sequence,
                OpaqueOperandKind::StoreQuery,
                digest,
            )
            .map_err(|_| AgentError::DigestUnavailable)
    };
    let intent = match call.tool_name.as_str() {
        "history.search" => StoreIntent::HistorySearch {
            tab,
            query: query()?,
            limit: limit()?,
        },
        "history.recent" => StoreIntent::HistoryRecent {
            tab,
            limit: limit()?,
        },
        "bookmarks.search" => StoreIntent::BookmarksSearch {
            tab,
            query: query()?,
            limit: limit()?,
        },
        "bookmarks.list" => StoreIntent::BookmarksList {
            tab,
            limit: limit()?,
        },
        "open_tabs.list" => StoreIntent::OpenTabsList { tab },
        _ => return Err(AgentError::ContradictoryReply),
    };
    Ok(ActionIntent::Store(intent))
}

fn optional_count(call: &ModelToolCall, name: &str) -> Option<u64> {
    call.arguments
        .iter()
        .find(|argument| argument.name == name)
        .and_then(|argument| match &argument.value {
            ArgumentValue::Count(value) => Some(*value),
            _ => None,
        })
}
