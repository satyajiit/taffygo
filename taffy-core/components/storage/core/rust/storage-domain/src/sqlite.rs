// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A host-only backend, for tests.
//!
//! It exists so every migration, lookup, and deletion in this crate is exercised
//! against a real database on a developer machine and in continuous
//! integration. It is compiled only under `cfg(test)`, and the library it uses
//! is a development dependency, so a shipping build links no second database
//! engine — which is the whole point of the backend trait.

use rusqlite::types::Value as SqlValue;

use crate::backend::{Connection, Executor, Row, Transaction, Value};
use crate::error::StorageError;

fn backend_error(operation: &'static str, error: &rusqlite::Error) -> StorageError {
    StorageError::Backend {
        operation,
        detail: error.to_string(),
    }
}

fn to_sql(value: &Value) -> SqlValue {
    match value {
        Value::Null => SqlValue::Null,
        Value::Integer(inner) => SqlValue::Integer(*inner),
        Value::Text(inner) => SqlValue::Text(inner.clone()),
        Value::Blob(inner) => SqlValue::Blob(inner.clone()),
    }
}

fn from_sql(value: SqlValue, column: usize) -> Result<Value, StorageError> {
    match value {
        SqlValue::Null => Ok(Value::Null),
        SqlValue::Integer(inner) => Ok(Value::Integer(inner)),
        SqlValue::Text(inner) => Ok(Value::Text(inner)),
        SqlValue::Blob(inner) => Ok(Value::Blob(inner)),
        SqlValue::Real(_) => Err(StorageError::ColumnType {
            column,
            expected: "an exact type; this schema stores no floating point",
        }),
    }
}

/// A test database handle.
#[derive(Debug)]
pub struct SqliteDatabase {
    inner: rusqlite::Connection,
}

impl SqliteDatabase {
    /// Opens an empty in-memory database.
    pub fn in_memory() -> Result<Self, StorageError> {
        let inner = rusqlite::Connection::open_in_memory()
            .map_err(|error| backend_error("opening a database", &error))?;
        Ok(Self { inner })
    }

    /// Runs a statement outside the crate's own transactions, for arranging a
    /// test fixture.
    pub fn raw_execute(&self, sql: &str) -> Result<(), StorageError> {
        self.inner
            .execute_batch(sql)
            .map_err(|error| backend_error("a fixture statement", &error))
    }

    /// Reads one column of text, for asserting on schema shape.
    pub fn raw_query_text(&self, sql: &str) -> Result<Vec<String>, StorageError> {
        let mut statement = self
            .inner
            .prepare(sql)
            .map_err(|error| backend_error("preparing a fixture query", &error))?;
        let mut rows = statement
            .query([])
            .map_err(|error| backend_error("running a fixture query", &error))?;
        let mut out = Vec::new();
        while let Some(row) = rows
            .next()
            .map_err(|error| backend_error("reading a fixture row", &error))?
        {
            let value: String = row
                .get(0)
                .map_err(|error| backend_error("reading a fixture column", &error))?;
            out.push(value);
        }
        Ok(out)
    }
}

impl Connection for SqliteDatabase {
    fn begin(&mut self) -> Result<Box<dyn Transaction + '_>, StorageError> {
        let mut transaction = self
            .inner
            .transaction()
            .map_err(|error| backend_error("beginning a transaction", &error))?;
        transaction.set_drop_behavior(rusqlite::DropBehavior::Rollback);
        Ok(Box::new(SqliteTransaction { inner: transaction }))
    }
}

struct SqliteTransaction<'a> {
    inner: rusqlite::Transaction<'a>,
}

impl Executor for SqliteTransaction<'_> {
    fn execute(&mut self, sql: &str, params: &[Value]) -> Result<u64, StorageError> {
        let bound: Vec<SqlValue> = params.iter().map(to_sql).collect();
        let changed = self
            .inner
            .execute(sql, rusqlite::params_from_iter(bound))
            .map_err(|error| backend_error("a statement", &error))?;
        Ok(changed as u64)
    }

    fn query(&mut self, sql: &str, params: &[Value]) -> Result<Vec<Row>, StorageError> {
        let bound: Vec<SqlValue> = params.iter().map(to_sql).collect();
        let mut statement = self
            .inner
            .prepare(sql)
            .map_err(|error| backend_error("preparing a query", &error))?;
        let width = statement.column_count();
        let mut rows = statement
            .query(rusqlite::params_from_iter(bound))
            .map_err(|error| backend_error("a query", &error))?;
        let mut out = Vec::new();
        while let Some(row) = rows
            .next()
            .map_err(|error| backend_error("reading a row", &error))?
        {
            let mut values = Vec::with_capacity(width);
            for column in 0..width {
                let value: SqlValue = row
                    .get(column)
                    .map_err(|error| backend_error("reading a column", &error))?;
                values.push(from_sql(value, column)?);
            }
            out.push(Row::new(values));
        }
        Ok(out)
    }
}

impl Transaction for SqliteTransaction<'_> {
    fn commit(self: Box<Self>) -> Result<(), StorageError> {
        self.inner
            .commit()
            .map_err(|error| backend_error("committing", &error))
    }

    fn rollback(self: Box<Self>) -> Result<(), StorageError> {
        self.inner
            .rollback()
            .map_err(|error| backend_error("rolling back", &error))
    }
}
