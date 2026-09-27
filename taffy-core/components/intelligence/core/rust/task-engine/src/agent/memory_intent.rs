// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projection of validated Memory calls into exact durable intents and
//! bounded generation-resident retrieval results.

use crate::action::{
    ActionIntent, MemoryIntent, MemoryScopeIntent, OpaqueOperandKind,
    DEFAULT_MEMORY_SEARCH_RESULTS, MAX_MEMORY_SEARCH_RESULTS,
};
use crate::tool::ArgumentValue;
use crate::workflow::WorkflowDigest;

use super::target::CallTarget;
use super::{AgentError, ModelToolCall, TurnResidency};

/// Maximum Memory material one local retrieval may add to one model request.
pub const MAX_MEMORY_TRANSCRIPT_RESULT_BYTES: usize = 64 * 1_024;
const OMITTED_NOTICE_RESERVE_BYTES: usize = 160;

/// One active standard Memory record selected for a task.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskMemorySearchEntry {
    memory_id: String,
    statement: String,
    source: String,
    scope: String,
}

impl TaskMemorySearchEntry {
    pub fn new(
        memory_id: String,
        statement: String,
        source: String,
        scope: String,
    ) -> Option<Self> {
        let valid = [
            memory_id.as_str(),
            statement.as_str(),
            source.as_str(),
            scope.as_str(),
        ]
        .into_iter()
        .all(valid_result_text);
        valid.then_some(Self {
            memory_id,
            statement,
            source,
            scope,
        })
    }

    fn render(&self) -> String {
        format!(
            "Memory {} — {} Scope: {}. Source: {}.",
            self.memory_id, self.statement, self.scope, self.source
        )
    }
}

/// Bounded model-visible Memory retrieval. It is never journalled.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskMemorySearchTranscriptOutcome {
    pieces: Vec<String>,
}

impl TaskMemorySearchTranscriptOutcome {
    pub fn bounded(entries: Vec<TaskMemorySearchEntry>) -> Self {
        if entries.is_empty() {
            return Self {
                pieces: vec!["No active Memory matched.".to_owned()],
            };
        }
        let mut pieces = vec!["Active Memory matches:".to_owned()];
        let mut spent = pieces.first().map_or(0, String::len);
        let mut omitted = 0_u32;
        for entry in entries {
            let rendered = entry.render();
            let fits_count = pieces.len() <= super::MAX_TASK_MEMORY_SEARCH_RESULTS;
            let fits_bytes = spent
                .saturating_add(rendered.len())
                .saturating_add(OMITTED_NOTICE_RESERVE_BYTES)
                <= MAX_MEMORY_TRANSCRIPT_RESULT_BYTES;
            if fits_count && fits_bytes {
                spent = spent.saturating_add(rendered.len());
                pieces.push(rendered);
            } else {
                omitted = omitted.saturating_add(1);
            }
        }
        if omitted > 0 {
            pieces.push(format!(
                "{omitted} additional Memory matches were omitted by the model-context bound."
            ));
        }
        Self { pieces }
    }

    pub const fn tool_name(&self) -> &'static str {
        "memory.search"
    }

    pub fn result_pieces(&self) -> &[String] {
        &self.pieces
    }
}

fn valid_result_text(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_MEMORY_TRANSCRIPT_RESULT_BYTES
        && value.trim() == value
        && !value.chars().any(char::is_control)
}

pub(super) fn memory_intent(
    call: &ModelToolCall,
    turn_ordinal: u64,
    sequence: u32,
    residency: &TurnResidency,
    target: &CallTarget,
    digest: &dyn WorkflowDigest,
) -> Result<ActionIntent, AgentError> {
    let tab = target.tab_id.clone();
    let intent = match call.tool_name.as_str() {
        "memory.search" => {
            let limit = optional_count(call, "limit")
                .map(u32::try_from)
                .transpose()
                .map_err(|_| AgentError::ContradictoryReply)?
                .unwrap_or(DEFAULT_MEMORY_SEARCH_RESULTS);
            if limit == 0 || limit > MAX_MEMORY_SEARCH_RESULTS {
                return Err(AgentError::ContradictoryReply);
            }
            MemoryIntent::Search {
                tab,
                query: resident_operand(
                    residency,
                    turn_ordinal,
                    sequence,
                    OpaqueOperandKind::MemoryQuery,
                    digest,
                )?,
                limit,
            }
        }
        "memory.save" => MemoryIntent::Save {
            tab,
            statement: resident_operand(
                residency,
                turn_ordinal,
                sequence,
                OpaqueOperandKind::MemoryStatement,
                digest,
            )?,
            scope: scope(call)?,
            expires_at_epoch_ms: optional_count(call, "expires_at"),
        },
        "memory.update" => MemoryIntent::Update {
            tab,
            memory_id: text(call, "memory")?,
            record_revision: count(call, "record_revision")?,
            statement: resident_operand(
                residency,
                turn_ordinal,
                sequence,
                OpaqueOperandKind::MemoryStatement,
                digest,
            )?,
            scope: scope(call)?,
            expires_at_epoch_ms: optional_count(call, "expires_at"),
        },
        "memory.delete" => MemoryIntent::Delete {
            tab,
            memory_id: text(call, "memory")?,
            record_revision: count(call, "record_revision")?,
        },
        _ => return Err(AgentError::ContradictoryReply),
    };
    Ok(ActionIntent::Memory(intent))
}

fn resident_operand(
    residency: &TurnResidency,
    turn_ordinal: u64,
    sequence: u32,
    kind: OpaqueOperandKind,
    digest: &dyn WorkflowDigest,
) -> Result<crate::action::OpaqueOperandRef, AgentError> {
    residency
        .action_operands()
        .bind(turn_ordinal, sequence, kind, digest)
        .map_err(|_| AgentError::DigestUnavailable)
}

fn scope(call: &ModelToolCall) -> Result<MemoryScopeIntent, AgentError> {
    match (choice(call, "scope"), optional_text(call, "workspace")) {
        (Some("all_tasks"), None) => Ok(MemoryScopeIntent::AllTasks),
        (Some("workspace"), Some(workspace_id)) => {
            Ok(MemoryScopeIntent::Workspace { workspace_id })
        }
        _ => Err(AgentError::ContradictoryReply),
    }
}

fn argument<'a>(call: &'a ModelToolCall, name: &str) -> Option<&'a ArgumentValue> {
    call.arguments
        .iter()
        .find(|argument| argument.name == name)
        .map(|argument| &argument.value)
}

fn text(call: &ModelToolCall, name: &str) -> Result<String, AgentError> {
    optional_text(call, name).ok_or(AgentError::ContradictoryReply)
}

fn optional_text(call: &ModelToolCall, name: &str) -> Option<String> {
    match argument(call, name) {
        Some(ArgumentValue::Text(value)) => Some(value.clone()),
        _ => None,
    }
}

fn choice<'a>(call: &'a ModelToolCall, name: &str) -> Option<&'a str> {
    match argument(call, name) {
        Some(ArgumentValue::Choice(value)) => Some(value),
        _ => None,
    }
}

fn count(call: &ModelToolCall, name: &str) -> Result<u64, AgentError> {
    optional_count(call, name).ok_or(AgentError::ContradictoryReply)
}

fn optional_count(call: &ModelToolCall, name: &str) -> Option<u64> {
    match argument(call, name) {
        Some(ArgumentValue::Count(value)) => Some(*value),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn retrieval_is_bounded_and_attributes_every_record() {
        let entry = TaskMemorySearchEntry::new(
            "memory-1".to_owned(),
            "Prefer short tables".to_owned(),
            "You wrote this".to_owned(),
            "all tasks".to_owned(),
        )
        .unwrap_or_else(|| unreachable!("valid Memory entry"));
        let outcome = TaskMemorySearchTranscriptOutcome::bounded(vec![entry]);
        assert_eq!(outcome.tool_name(), "memory.search");
        assert!(outcome.result_pieces().iter().any(|piece| {
            piece.contains("Prefer short tables") && piece.contains("You wrote this")
        }));
        assert!(
            outcome
                .result_pieces()
                .iter()
                .map(String::len)
                .sum::<usize>()
                <= MAX_MEMORY_TRANSCRIPT_RESULT_BYTES
        );
    }
}
