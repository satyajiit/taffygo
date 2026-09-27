// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The database boundary.
//!
//! These traits are a portable reference boundary for domain algorithms. Host
//! tests bind a plain library implementation; no production database adapter is
//! linked here. The browser-owned storage broker separately adapts Chromium SQL.
//!
//! The test backend is not a shipping dependency. The implementation lives
//! behind a test-only module and cannot reach a release build; the isolated core
//! receives typed commit completions rather than a database connection.
//!
//! The traits are deliberately small and object safe. A backend supplies
//! parameterized statements and rows of scalars; everything above them —
//! schema, migration order, retrieval, deletion — is ordinary code in this
//! crate, testable without a database at all where it does not need one.

use crate::error::StorageError;

/// A scalar a statement can carry or return.
///
/// There is no floating-point variant on purpose. Every quantity this schema
/// stores is exact — counts, token totals, and confidences in basis points —
/// and a value that changes when it round-trips is a value deletion
/// verification cannot compare.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Value {
    /// SQL null.
    Null,
    /// A 64-bit integer.
    Integer(i64),
    /// Text.
    Text(String),
    /// A byte string.
    Blob(Vec<u8>),
}

impl Value {
    /// Wraps text.
    pub fn text(value: impl Into<String>) -> Self {
        Self::Text(value.into())
    }

    /// Wraps optional text.
    pub fn maybe_text(value: Option<impl Into<String>>) -> Self {
        value.map_or(Self::Null, |inner| Self::Text(inner.into()))
    }

    /// Wraps a boolean as the integer the schema stores.
    pub fn boolean(value: bool) -> Self {
        Self::Integer(i64::from(value))
    }
}

/// One returned row, read by position.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Row {
    values: Vec<Value>,
}

impl Row {
    /// Builds a row.
    pub fn new(values: Vec<Value>) -> Self {
        Self { values }
    }

    /// How many columns it has.
    pub fn len(&self) -> usize {
        self.values.len()
    }

    /// Whether it has no columns.
    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }

    /// The raw value at `column`.
    pub fn value(&self, column: usize) -> Result<&Value, StorageError> {
        self.values
            .get(column)
            .ok_or(StorageError::ColumnMissing { column })
    }

    /// The integer at `column`.
    pub fn integer(&self, column: usize) -> Result<i64, StorageError> {
        match self.value(column)? {
            Value::Integer(value) => Ok(*value),
            _ => Err(StorageError::ColumnType {
                column,
                expected: "an integer",
            }),
        }
    }

    /// The boolean at `column`, stored as an integer.
    pub fn boolean(&self, column: usize) -> Result<bool, StorageError> {
        Ok(self.integer(column)? != 0)
    }

    /// The text at `column`.
    pub fn text(&self, column: usize) -> Result<&str, StorageError> {
        match self.value(column)? {
            Value::Text(value) => Ok(value),
            _ => Err(StorageError::ColumnType {
                column,
                expected: "text",
            }),
        }
    }

    /// The text at `column`, or `None` for null.
    pub fn maybe_text(&self, column: usize) -> Result<Option<&str>, StorageError> {
        match self.value(column)? {
            Value::Null => Ok(None),
            Value::Text(value) => Ok(Some(value)),
            _ => Err(StorageError::ColumnType {
                column,
                expected: "text or null",
            }),
        }
    }
}

/// Runs statements.
#[allow(clippy::module_name_repetitions)]
pub trait Executor {
    /// Runs a statement that returns no rows, answering how many it changed.
    fn execute(&mut self, sql: &str, params: &[Value]) -> Result<u64, StorageError>;

    /// Runs a statement that returns rows.
    fn query(&mut self, sql: &str, params: &[Value]) -> Result<Vec<Row>, StorageError>;
}

/// A unit of work that either lands whole or not at all.
pub trait Transaction: Executor {
    /// Makes the work durable.
    fn commit(self: Box<Self>) -> Result<(), StorageError>;

    /// Discards it.
    fn rollback(self: Box<Self>) -> Result<(), StorageError>;
}

/// A database handle.
pub trait Connection {
    /// Opens a transaction.
    fn begin(&mut self) -> Result<Box<dyn Transaction + '_>, StorageError>;
}

/// Reads the single integer a counting query returns.
pub fn count(
    executor: &mut dyn Executor,
    sql: &str,
    params: &[Value],
) -> Result<i64, StorageError> {
    let rows = executor.query(sql, params)?;
    rows.first()
        .ok_or(StorageError::RowMissing { what: "a count" })?
        .integer(0)
}
