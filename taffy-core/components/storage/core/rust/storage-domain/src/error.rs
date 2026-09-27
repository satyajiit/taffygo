// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What can go wrong, as values.
//!
//! Nothing in this crate panics. The portable domain runs inside the isolated
//! core service and receives only typed broker data; a malformed or conflicting
//! value must fail the operation without crossing the service boundary. Every
//! failure is a `Result`, and every variant identifies the statement,
//! migration, or column that produced it.

use core::fmt;

/// A storage failure.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StorageError {
    /// The backend refused a statement.
    Backend {
        /// What the caller was doing.
        operation: &'static str,
        /// What the backend said.
        detail: String,
    },
    /// A column held a type the caller did not expect.
    ColumnType {
        /// Column position.
        column: usize,
        /// The expected type.
        expected: &'static str,
    },
    /// A row was shorter than the caller expected.
    ColumnMissing {
        /// Column position.
        column: usize,
    },
    /// A query that had to return a row returned none.
    RowMissing {
        /// What the caller was looking for.
        what: &'static str,
    },
    /// A stored value did not parse back into its type.
    Malformed {
        /// What failed to parse.
        what: &'static str,
        /// The value, or a bounded description of it.
        detail: String,
    },
    /// A migration recorded in the database no longer matches the one compiled
    /// into this build.
    ///
    /// Editing an applied migration is the one thing a forward-only system
    /// cannot recover from, so it is refused loudly rather than reconciled.
    MigrationChanged {
        /// Which migration.
        version: u32,
        /// The checksum in the database.
        recorded: String,
        /// The checksum in this build.
        compiled: String,
    },
    /// The database has a migration this build does not know about.
    ///
    /// A newer build wrote it. Continuing would mean writing rows a newer
    /// schema owns, so this build stops instead.
    SchemaFromNewerBuild {
        /// Highest version in the database.
        database: u32,
        /// Highest version in this build.
        build: u32,
    },
    /// A deletion finished without satisfying its own verification.
    ///
    /// The transaction is rolled back: a deletion that cannot be verified is a
    /// deletion that did not happen.
    DeletionUnverified {
        /// What was still reachable.
        detail: String,
    },
    /// A workspace does not exist or has already been deleted.
    WorkspaceNotFound,
    /// A workspace command was computed against a revision that is no longer current.
    WorkspaceRevisionConflict { expected: u64, actual: u64 },
    /// A delete confirmation did not bind the exact current workspace state.
    InvalidWorkspaceConfirmation,
}

impl fmt::Display for StorageError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Backend { operation, detail } => write!(f, "{operation} failed: {detail}"),
            Self::ColumnType { column, expected } => {
                write!(f, "column {column} is not {expected}")
            }
            Self::ColumnMissing { column } => write!(f, "column {column} is missing"),
            Self::RowMissing { what } => write!(f, "no row for {what}"),
            Self::Malformed { what, detail } => write!(f, "malformed {what}: {detail}"),
            Self::MigrationChanged {
                version,
                recorded,
                compiled,
            } => write!(
                f,
                "migration {version} changed after it was applied: {recorded} became {compiled}"
            ),
            Self::SchemaFromNewerBuild { database, build } => write!(
                f,
                "database is at schema {database} and this build knows {build}"
            ),
            Self::DeletionUnverified { detail } => {
                write!(f, "deletion could not be verified: {detail}")
            }
            Self::WorkspaceNotFound => f.write_str("workspace does not exist"),
            Self::WorkspaceRevisionConflict { expected, actual } => write!(
                f,
                "workspace revision changed: expected {expected}, current {actual}"
            ),
            Self::InvalidWorkspaceConfirmation => {
                f.write_str("workspace deletion confirmation is not current")
            }
        }
    }
}
