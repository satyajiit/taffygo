// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Full-text retrieval, and the index that deletion has to be able to empty.
//!
//! Retrieval is structured lookup plus full text. No vector index is created
//! and none is anticipated: an approximate-nearest-neighbour engine is adopted
//! only if the benchmark proves full text insufficient, and building the schema
//! around one first would make that decision for everyone.
//!
//! The index is derived and rebuildable, and every entry names the source it
//! came from. That is what makes verified deletion possible: removing a source
//! can find every index entry it produced without parsing any of them.
//!
//! Query text is treated as text. A search term is quoted into a phrase before
//! it reaches the matcher, so a value copied out of a page cannot turn into a
//! query expression.

use crate::backend::{Executor, Value};
use crate::clock::Timestamp;
use crate::error::StorageError;
use crate::ids::{ArtifactId, DocumentId, FactId, ObservationId, SourceId, WorkspaceId};

/// Finds an ASCII needle without allocating a lower-cased copy of either side.
///
/// Library and Memory queries run over bounded in-memory projections before a
/// full-text index is available. Their common case is ASCII, and constructing
/// one combined lower-cased document for every record made each keystroke pay
/// for the entire collection. Non-ASCII text keeps its Unicode lower-casing
/// path in those callers; this helper is deliberately only the exact ASCII
/// fast path.
pub(crate) fn contains_ascii_case_insensitive(haystack: &str, needle: &str) -> bool {
    debug_assert!(haystack.is_ascii());
    debug_assert!(needle.is_ascii());
    if needle.is_empty() {
        return true;
    }
    if needle.len() > haystack.len() {
        return false;
    }
    haystack
        .as_bytes()
        .windows(needle.len())
        .any(|window| window.eq_ignore_ascii_case(needle.as_bytes()))
}

/// What an index entry is about.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IndexedRecord {
    /// An observation of a source.
    Observation(ObservationId),
    /// A normalized fact.
    Fact(FactId),
    /// A produced artifact.
    Artifact(ArtifactId),
}

impl IndexedRecord {
    /// The stored type name.
    pub fn record_type(self) -> &'static str {
        match self {
            Self::Observation(_) => "OBSERVATION",
            Self::Fact(_) => "FACT",
            Self::Artifact(_) => "ARTIFACT",
        }
    }

    /// The stored identifier.
    pub fn record_id(self) -> String {
        match self {
            Self::Observation(id) => id.to_text(),
            Self::Fact(id) => id.to_text(),
            Self::Artifact(id) => id.to_text(),
        }
    }
}

/// One entry to index.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SearchDocument {
    /// Identity of the entry itself.
    pub document_id: DocumentId,
    /// What it is about.
    pub record: IndexedRecord,
    /// The workspace it belongs to, when it belongs to one.
    pub workspace_id: Option<WorkspaceId>,
    /// The source it derives from, when it derives from one.
    pub source_id: Option<SourceId>,
    /// Which retention class governs it.
    pub retention_class: String,
    /// The heading text.
    pub title: String,
    /// The body text.
    pub body: String,
}

/// One match.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SearchHit {
    /// The entry that matched.
    pub document_id: DocumentId,
    /// What kind of record it is about.
    pub record_type: String,
    /// Which record.
    pub record_id: String,
    /// Its workspace, when it has one.
    pub workspace_id: Option<String>,
    /// Its source, when it has one.
    pub source_id: Option<String>,
}

/// Quotes a term so the matcher reads it as text rather than as syntax.
pub fn phrase(term: &str) -> String {
    let mut out = String::with_capacity(term.len() + 2);
    out.push('"');
    for character in term.chars() {
        if character == '"' {
            out.push('"');
        }
        out.push(character);
    }
    out.push('"');
    out
}

/// Adds an entry to the index.
pub fn index(
    executor: &mut dyn Executor,
    document: &SearchDocument,
    indexed_at: &Timestamp,
) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO search_document \
         (document_id, record_type, record_id, workspace_id, source_id, retention_class, indexed_at_utc) \
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
        &[
            Value::text(document.document_id.to_text()),
            Value::text(document.record.record_type()),
            Value::text(document.record.record_id()),
            Value::maybe_text(document.workspace_id.map(WorkspaceId::to_text)),
            Value::maybe_text(document.source_id.map(SourceId::to_text)),
            Value::text(document.retention_class.clone()),
            Value::text(indexed_at.as_str()),
        ],
    )?;
    executor.execute(
        "INSERT INTO search_index (document_id, title, body) VALUES (?1, ?2, ?3)",
        &[
            Value::text(document.document_id.to_text()),
            Value::text(document.title.clone()),
            Value::text(document.body.clone()),
        ],
    )?;
    Ok(())
}

/// Finds entries containing `term`, in identity order.
///
/// Identity order rather than relevance order, because a lookup two devices
/// disagree about is a lookup nobody can test. Ranking belongs above this
/// layer, where it can be evaluated against the benchmark.
pub fn search(
    executor: &mut dyn Executor,
    term: &str,
    limit: u32,
) -> Result<Vec<SearchHit>, StorageError> {
    let rows = executor.query(
        "SELECT d.document_id, d.record_type, d.record_id, d.workspace_id, d.source_id \
         FROM search_document AS d \
         WHERE d.document_id IN \
           (SELECT document_id FROM search_index WHERE search_index MATCH ?1) \
         ORDER BY d.document_id LIMIT ?2",
        &[Value::text(phrase(term)), Value::Integer(i64::from(limit))],
    )?;
    rows.iter()
        .map(|row| {
            let raw = row.text(0)?;
            Ok(SearchHit {
                document_id: DocumentId::parse(raw).map_err(|_| StorageError::Malformed {
                    what: "a search document id",
                    detail: raw.to_owned(),
                })?,
                record_type: row.text(1)?.to_owned(),
                record_id: row.text(2)?.to_owned(),
                workspace_id: row.maybe_text(3)?.map(str::to_owned),
                source_id: row.maybe_text(4)?.map(str::to_owned),
            })
        })
        .collect()
}

/// Removes every index entry derived from a source.
pub fn remove_for_source(
    executor: &mut dyn Executor,
    source_id: SourceId,
) -> Result<u64, StorageError> {
    let key = Value::text(source_id.to_text());
    let removed = executor.execute(
        "DELETE FROM search_index WHERE document_id IN \
         (SELECT document_id FROM search_document WHERE source_id = ?1)",
        std::slice::from_ref(&key),
    )?;
    executor.execute("DELETE FROM search_document WHERE source_id = ?1", &[key])?;
    Ok(removed)
}

/// Removes every index entry about one record.
pub fn remove_for_record(
    executor: &mut dyn Executor,
    record_type: &str,
    record_id: &str,
) -> Result<u64, StorageError> {
    let params = [Value::text(record_type), Value::text(record_id)];
    let removed = executor.execute(
        "DELETE FROM search_index WHERE document_id IN \
         (SELECT document_id FROM search_document WHERE record_type = ?1 AND record_id = ?2)",
        &params,
    )?;
    executor.execute(
        "DELETE FROM search_document WHERE record_type = ?1 AND record_id = ?2",
        &params,
    )?;
    Ok(removed)
}

/// Counts index rows with no entry behind them.
///
/// Always zero. Deletion verification asserts it, which catches the failure
/// mode a text index makes easy: removing the record and leaving the terms.
pub fn orphaned_index_rows(executor: &mut dyn Executor) -> Result<i64, StorageError> {
    crate::backend::count(
        executor,
        "SELECT COUNT(*) FROM search_index \
         WHERE document_id NOT IN (SELECT document_id FROM search_document)",
        &[],
    )
}
