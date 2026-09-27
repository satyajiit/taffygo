// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projection of validated Library calls into exact durable intents.

use std::fmt::Write as _;

use crate::action::{
    ActionIntent, LibraryIntent, OpaqueOperandKind, DEFAULT_LIBRARY_SEARCH_RESULTS,
    MAX_LIBRARY_SEARCH_RESULTS,
};
use crate::tool::ArgumentValue;
use crate::workflow::WorkflowDigest;

use super::target::CallTarget;
use super::{AgentError, ModelToolCall, TurnResidency};

/// Maximum saved-Library material one resident tool result may add to a
/// model request. The global transcript ladder remains authoritative; this
/// tighter half-window bound prevents one retrieval from monopolizing it.
pub const MAX_LIBRARY_TRANSCRIPT_RESULT_BYTES: usize = 64 * 1_024;

const MAX_LIBRARY_TRANSCRIPT_SOURCES: usize = 16;
const OMITTED_NOTICE_RESERVE_BYTES: usize = 192;

/// One citation attached to a person-kept Library fact.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskLibraryCitation {
    title: String,
    host: String,
}

impl TaskLibraryCitation {
    /// Builds one bounded, printable citation.
    pub fn new(title: String, host: String) -> Option<Self> {
        (valid_result_text(&title) && valid_result_text(&host)).then_some(Self { title, host })
    }
}

/// One saved fact selected by the deterministic Library search.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskLibrarySearchEntry {
    entry_id: String,
    collection: String,
    field: String,
    value: String,
    citations: Vec<TaskLibraryCitation>,
    age_ms: u64,
    has_conflict: bool,
}

impl TaskLibrarySearchEntry {
    /// Creates one content-bearing but generation-resident search entry.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        entry_id: String,
        collection: String,
        field: String,
        value: String,
        citations: Vec<TaskLibraryCitation>,
        age_ms: u64,
        has_conflict: bool,
    ) -> Option<Self> {
        (valid_result_text(&entry_id)
            && valid_result_text(&collection)
            && valid_result_text(&field)
            && valid_result_text(&value)
            && !citations.is_empty()
            && citations.len() <= MAX_LIBRARY_TRANSCRIPT_SOURCES)
            .then_some(Self {
                entry_id,
                collection,
                field,
                value,
                citations,
                age_ms,
                has_conflict,
            })
    }

    fn render(&self) -> String {
        let mut result = format!(
            "Saved fact {} — {}: {}. Collection: {}. Sources: ",
            self.entry_id, self.field, self.value, self.collection
        );
        for (index, citation) in self.citations.iter().enumerate() {
            if index > 0 {
                result.push_str("; ");
            }
            let _ = write!(result, "{} ({})", citation.title, citation.host);
        }
        let conflict = if self.has_conflict {
            " A source conflict is recorded."
        } else {
            " No source conflict is recorded."
        };
        let _ = write!(result, ". Last checked {} ms ago.{}", self.age_ms, conflict);
        result
    }
}

/// Bounded model-visible Library search material. It lives only beside the
/// exact model reply that authored the call and is never journalled.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskLibrarySearchTranscriptOutcome {
    pieces: Vec<String>,
}

impl TaskLibrarySearchTranscriptOutcome {
    /// Keeps deterministic prefix results until the retrieval-specific byte
    /// bound is reached and explicitly reports every omitted match.
    pub fn bounded(entries: Vec<TaskLibrarySearchEntry>) -> Self {
        if entries.is_empty() {
            return Self {
                pieces: vec!["No saved Library facts matched.".to_owned()],
            };
        }
        let mut pieces = vec!["Saved Library matches:".to_owned()];
        let mut spent = pieces.first().map_or(0, String::len);
        let mut omitted = 0_u32;
        for entry in entries {
            let rendered = entry.render();
            let fits_count = pieces.len() <= super::MAX_TASK_LIBRARY_SEARCH_RESULTS;
            let fits_bytes = spent
                .saturating_add(rendered.len())
                .saturating_add(OMITTED_NOTICE_RESERVE_BYTES)
                <= MAX_LIBRARY_TRANSCRIPT_RESULT_BYTES;
            if fits_count && fits_bytes {
                spent = spent.saturating_add(rendered.len());
                pieces.push(rendered);
            } else {
                omitted = omitted.saturating_add(1);
            }
        }
        if omitted > 0 {
            pieces.push(format!(
                "{omitted} additional saved Library matches were omitted by the model-context bound."
            ));
        }
        Self { pieces }
    }

    /// The registry name this result is allowed to answer.
    pub const fn tool_name(&self) -> &'static str {
        "library.search"
    }

    /// Borrowed pieces for one request write.
    pub fn result_pieces(&self) -> &[String] {
        &self.pieces
    }
}

fn valid_result_text(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_LIBRARY_TRANSCRIPT_RESULT_BYTES
        && value.trim() == value
        && !value.chars().any(char::is_control)
}

pub(super) fn library_intent(
    call: &ModelToolCall,
    turn_ordinal: u64,
    sequence: u32,
    residency: &TurnResidency,
    target: &CallTarget,
    digest: &dyn WorkflowDigest,
) -> Result<ActionIntent, AgentError> {
    let tab = target.tab_id.clone();
    let intent = match call.tool_name.as_str() {
        "library.search" => {
            let limit = optional_count(call, "limit")
                .map(u32::try_from)
                .transpose()
                .map_err(|_| AgentError::ContradictoryReply)?
                .unwrap_or(DEFAULT_LIBRARY_SEARCH_RESULTS);
            if limit == 0 || limit > MAX_LIBRARY_SEARCH_RESULTS {
                return Err(AgentError::ContradictoryReply);
            }
            LibraryIntent::Search {
                tab,
                query: residency
                    .action_operands()
                    .bind(
                        turn_ordinal,
                        sequence,
                        OpaqueOperandKind::LibraryQuery,
                        digest,
                    )
                    .map_err(|_| AgentError::DigestUnavailable)?,
                limit,
            }
        }
        "library.save" => LibraryIntent::Save {
            tab,
            workspace_id: text(call, "workspace")?,
            workspace_revision: count(call, "workspace_revision")?,
            fact_id: text(call, "fact")?,
            entry_revision: count(call, "entry_revision")?,
        },
        "library.remove" => LibraryIntent::Remove {
            tab,
            entry_id: text(call, "entry")?,
            entry_revision: count(call, "entry_revision")?,
        },
        _ => return Err(AgentError::ContradictoryReply),
    };
    Ok(ActionIntent::Library(intent))
}

fn argument<'a>(call: &'a ModelToolCall, name: &str) -> Option<&'a ArgumentValue> {
    call.arguments
        .iter()
        .find(|argument| argument.name == name)
        .map(|argument| &argument.value)
}

fn text(call: &ModelToolCall, name: &str) -> Result<String, AgentError> {
    match argument(call, name) {
        Some(ArgumentValue::Text(value)) => Ok(value.clone()),
        _ => Err(AgentError::ContradictoryReply),
    }
}

fn count(call: &ModelToolCall, name: &str) -> Result<u64, AgentError> {
    match argument(call, name) {
        Some(ArgumentValue::Count(value)) => Ok(*value),
        _ => Err(AgentError::ContradictoryReply),
    }
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

    fn entry(id: &str, value: String, conflict: bool) -> TaskLibrarySearchEntry {
        TaskLibrarySearchEntry::new(
            id.to_owned(),
            "Battery research".to_owned(),
            "battery life".to_owned(),
            value,
            vec![TaskLibraryCitation::new(
                "Independent test".to_owned(),
                "example.test".to_owned(),
            )
            .unwrap_or_else(|| unreachable!("valid citation"))],
            125,
            conflict,
        )
        .unwrap_or_else(|| unreachable!("valid saved fact"))
    }

    #[test]
    fn a_result_carries_value_citation_freshness_and_conflict() {
        let outcome = TaskLibrarySearchTranscriptOutcome::bounded(vec![entry(
            "entry-1",
            "twelve hours".to_owned(),
            true,
        )]);
        let text = outcome.result_pieces().join("\n");

        assert!(text.contains("battery life: twelve hours"));
        assert!(text.contains("Independent test (example.test)"));
        assert!(text.contains("Last checked 125 ms ago"));
        assert!(text.contains("source conflict is recorded"));
    }

    #[test]
    fn retrieval_is_prefix_bounded_and_names_every_omission() {
        let outcome = TaskLibrarySearchTranscriptOutcome::bounded(vec![
            entry("entry-1", "v".repeat(40_000), false),
            entry("entry-2", "w".repeat(40_000), false),
        ]);
        let pieces = outcome.result_pieces();
        let bytes = pieces.iter().map(String::len).sum::<usize>();

        assert!(bytes <= MAX_LIBRARY_TRANSCRIPT_RESULT_BYTES);
        assert!(pieces.iter().any(|piece| piece.contains("entry-1")));
        assert!(pieces.iter().all(|piece| !piece.contains("entry-2")));
        assert!(pieces
            .last()
            .is_some_and(|piece| piece.starts_with("1 additional saved Library match")));
    }

    #[test]
    fn an_empty_search_is_an_explicit_result() {
        let outcome = TaskLibrarySearchTranscriptOutcome::bounded(Vec::new());
        assert_eq!(
            outcome.result_pieces(),
            &["No saved Library facts matched.".to_owned()]
        );
    }
}
