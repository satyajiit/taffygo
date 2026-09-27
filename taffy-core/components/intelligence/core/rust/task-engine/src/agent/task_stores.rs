// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded, generation-resident rows answered from a person's own stores.
//!
//! The browser executes every store read and answers references, never
//! pages: a title, a host, a path with its query and fragment removed, and a
//! time (decision 0133 section 3). The rows are installed beside the model
//! reply that asked for them after the outcome's commit and are never
//! journalled with the durable task.

use crate::action::MAX_STORE_RESULTS;

/// Most rows one store read may hand to a model.
pub const MAX_TASK_STORE_RESULTS: usize = MAX_STORE_RESULTS as usize;
/// Maximum store material one read may add to one model request.
pub const MAX_STORE_TRANSCRIPT_RESULT_BYTES: usize = 64 * 1_024;
/// Longest title, host or path one row may carry.
pub const MAX_STORE_ROW_FIELD_BYTES: usize = 512;
const OMITTED_NOTICE_RESERVE_BYTES: usize = 160;

/// One reference into a person's store.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskStoreRow {
    title: String,
    host: String,
    path: String,
    when_utc_ms: u64,
}

impl TaskStoreRow {
    /// Accepts one row only when every field is bounded, trimmed, free of
    /// control characters, and the path carries no query or fragment.
    pub fn new(title: String, host: String, path: String, when_utc_ms: u64) -> Option<Self> {
        let bounded = |value: &str| {
            value.len() <= MAX_STORE_ROW_FIELD_BYTES
                && value.trim() == value
                && !value.chars().any(char::is_control)
        };
        let path_is_plain = !path.contains('?') && !path.contains('#');
        (bounded(&title) && bounded(&host) && !host.is_empty() && bounded(&path) && path_is_plain)
            .then_some(Self {
                title,
                host,
                path,
                when_utc_ms,
            })
    }

    pub fn title(&self) -> &str {
        &self.title
    }

    pub fn host(&self) -> &str {
        &self.host
    }

    pub fn path(&self) -> &str {
        &self.path
    }

    pub const fn when_utc_ms(&self) -> u64 {
        self.when_utc_ms
    }

    fn render(&self, index: usize) -> String {
        let title = if self.title.is_empty() {
            "(untitled)"
        } else {
            self.title.as_str()
        };
        let path = if self.path.is_empty() {
            "/"
        } else {
            self.path.as_str()
        };
        format!(
            "{index}. {title} — {}{path} (t={})",
            self.host, self.when_utc_ms
        )
    }
}

/// A store read whose rows the browser verified.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskStoreActionResult {
    tool_name: &'static str,
    rows: Vec<TaskStoreRow>,
    omitted: u32,
}

/// Why an asserted store result cannot become model context.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskStoreResultError {
    UnknownTool,
    TooManyRows,
}

impl TaskStoreActionResult {
    /// Accepts a result only for one of the five store tools and only within
    /// the row cap. `omitted` is the browser's own count of rows it left out.
    pub fn new(
        tool_name: &str,
        rows: Vec<TaskStoreRow>,
        omitted: u32,
    ) -> Result<Self, TaskStoreResultError> {
        let tool_name = STORE_TOOLS
            .iter()
            .copied()
            .find(|name| *name == tool_name)
            .ok_or(TaskStoreResultError::UnknownTool)?;
        if rows.len() > MAX_TASK_STORE_RESULTS {
            return Err(TaskStoreResultError::TooManyRows);
        }
        Ok(Self {
            tool_name,
            rows,
            omitted,
        })
    }

    pub const fn tool_name(&self) -> &'static str {
        self.tool_name
    }

    pub fn rows(&self) -> &[TaskStoreRow] {
        &self.rows
    }

    pub const fn omitted(&self) -> u32 {
        self.omitted
    }

    /// The bounded transcript rendering of these rows.
    pub fn transcript_outcome(&self) -> TaskStoreTranscriptOutcome {
        TaskStoreTranscriptOutcome::bounded(self.tool_name, &self.rows, self.omitted)
    }
}

/// The five store tools, in registry order.
pub const STORE_TOOLS: &[&str] = &[
    "history.search",
    "history.recent",
    "bookmarks.search",
    "bookmarks.list",
    "open_tabs.list",
];

/// Bounded model-visible store rows. Never journalled.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskStoreTranscriptOutcome {
    tool_name: &'static str,
    pieces: Vec<String>,
}

impl TaskStoreTranscriptOutcome {
    fn bounded(tool_name: &'static str, rows: &[TaskStoreRow], browser_omitted: u32) -> Self {
        if rows.is_empty() {
            return Self {
                tool_name,
                pieces: vec![empty_line(tool_name).to_owned()],
            };
        }
        let mut pieces = vec![heading(tool_name).to_owned()];
        let mut spent = pieces.first().map_or(0, String::len);
        let mut omitted = browser_omitted;
        for (index, row) in rows.iter().enumerate() {
            let rendered = row.render(index.saturating_add(1));
            let fits_bytes = spent
                .saturating_add(rendered.len())
                .saturating_add(OMITTED_NOTICE_RESERVE_BYTES)
                <= MAX_STORE_TRANSCRIPT_RESULT_BYTES;
            if fits_bytes {
                spent = spent.saturating_add(rendered.len());
                pieces.push(rendered);
            } else {
                omitted = omitted.saturating_add(1);
            }
        }
        if omitted > 0 {
            pieces.push(format!(
                "{omitted} more rows were left out; narrow the words or lower the limit."
            ));
        }
        Self { tool_name, pieces }
    }

    pub const fn tool_name(&self) -> &'static str {
        self.tool_name
    }

    pub fn result_pieces(&self) -> &[String] {
        &self.pieces
    }
}

const fn heading(tool_name: &str) -> &'static str {
    match tool_name.as_bytes() {
        b"history.search" => "History visits matching the words:",
        b"history.recent" => "Latest history visits:",
        b"bookmarks.search" => "Bookmarks matching the words:",
        b"bookmarks.list" => "Bookmarks:",
        _ => "The person's open tabs:",
    }
}

const fn empty_line(tool_name: &str) -> &'static str {
    match tool_name.as_bytes() {
        b"history.search" => "No history visit matched the words.",
        b"history.recent" => "The history is empty.",
        b"bookmarks.search" => "No bookmark matched the words.",
        b"bookmarks.list" => "There are no bookmarks.",
        _ => "The person has no other open tabs.",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn row(index: u64) -> TaskStoreRow {
        TaskStoreRow::new(
            format!("Page {index}"),
            "example.test".to_owned(),
            format!("/p/{index}"),
            1_000 + index,
        )
        .unwrap_or_else(|| unreachable!("valid row"))
    }

    #[test]
    fn a_row_carries_no_query_or_fragment_and_stays_bounded() {
        assert!(TaskStoreRow::new("t".into(), "h.test".into(), "/a?b=1".into(), 1).is_none());
        assert!(TaskStoreRow::new("t".into(), "h.test".into(), "/a#x".into(), 1).is_none());
        assert!(TaskStoreRow::new("t".into(), String::new(), "/a".into(), 1).is_none());
        assert!(TaskStoreRow::new("t\n".into(), "h.test".into(), "/a".into(), 1).is_none());
        let long = "x".repeat(MAX_STORE_ROW_FIELD_BYTES + 1);
        assert!(TaskStoreRow::new(long, "h.test".into(), "/a".into(), 1).is_none());
        assert!(TaskStoreRow::new(String::new(), "h.test".into(), String::new(), 1).is_some());
    }

    #[test]
    fn a_result_names_one_store_tool_and_is_row_capped() {
        assert_eq!(
            TaskStoreActionResult::new("memory.search", Vec::new(), 0),
            Err(TaskStoreResultError::UnknownTool)
        );
        let too_many = (0..=MAX_TASK_STORE_RESULTS as u64).map(row).collect();
        assert_eq!(
            TaskStoreActionResult::new("history.recent", too_many, 0),
            Err(TaskStoreResultError::TooManyRows)
        );
        let result = TaskStoreActionResult::new("bookmarks.list", vec![row(1), row(2)], 3)
            .unwrap_or_else(|_| unreachable!("two rows"));
        let outcome = result.transcript_outcome();
        assert_eq!(outcome.tool_name(), "bookmarks.list");
        assert_eq!(outcome.result_pieces().len(), 4);
        assert!(outcome.result_pieces()[1].contains("example.test/p/1"));
        assert!(outcome.result_pieces()[3].starts_with("3 more rows"));
        let empty = TaskStoreActionResult::new("open_tabs.list", Vec::new(), 0)
            .unwrap_or_else(|_| unreachable!("no rows"));
        assert_eq!(
            empty.transcript_outcome().result_pieces(),
            ["The person has no other open tabs.".to_owned()]
        );
    }
}
