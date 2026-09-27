// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic table reshaping behind one pure, bounded module boundary.
//!
//! [`reshape`] retains lineage; [`reshape_to_csv`] moves the same already
//! validated encoding directly to a caller that only needs the exported
//! result. Parsing, exact decimal arithmetic, ordering, grouping, pivoting,
//! CSV quoting, spreadsheet-safe export, and lineage accounting stay inside
//! the implementation. The module reads no clock, file, network resource,
//! environment variable, or random source.

mod csv;
mod decimal;
mod error;
mod json;
mod limits;
mod model;
mod recipe;
mod transform;

pub use error::{CsvErrorKind, Error};
pub use limits::Limits;
pub use model::{CellRef, SourcedCell, SourcedTable, Table};
pub use recipe::{
    Aggregate, AggregateFunction, ComparisonMode, Direction, Keep, OrderKey, Predicate, Recipe,
    Step,
};

/// A cooperative cancellation source for long pure transforms.
pub trait Cancellation {
    /// Whether the caller no longer needs the result.
    fn is_cancelled(&self) -> bool;
}

/// A cancellation source for synchronous callers that always need the result.
#[derive(Clone, Copy, Debug, Default)]
pub struct NeverCancelled;

impl Cancellation for NeverCancelled {
    fn is_cancelled(&self) -> bool {
        false
    }
}

/// One fully validated comma-separated reshape and its exact dimensions.
///
/// The bytes are produced by the reshape pass itself. Consuming them avoids
/// repeating spreadsheet hardening, quoting, and output-bound accounting.
pub struct ReshapedCsv {
    bytes: Vec<u8>,
    rows: usize,
    columns: usize,
}

impl std::fmt::Debug for ReshapedCsv {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("ReshapedCsv")
            .field("byte_len", &self.bytes.len())
            .field("rows", &self.rows)
            .field("columns", &self.columns)
            .finish_non_exhaustive()
    }
}

impl ReshapedCsv {
    /// Number of data rows, excluding the header.
    pub const fn rows(&self) -> usize {
        self.rows
    }

    /// Number of output columns.
    pub const fn columns(&self) -> usize {
        self.columns
    }

    /// Moves out the strict RFC 4180 bytes without another allocation.
    pub fn into_bytes(self) -> Vec<u8> {
        self.bytes
    }
}

/// Reshapes one parsed table according to one closed, typed recipe.
///
/// The result retains bounded input-cell and input-row lineage. Every
/// exceeded bound is an error and no partial table is returned.
pub fn reshape(table: Table, recipe: &Recipe, limits: Limits) -> Result<SourcedTable, Error> {
    reshape_with_cancellation(table, recipe, limits, &NeverCancelled)
}

/// Reshapes and returns the already validated comma-separated output.
///
/// Use this when lineage is not needed after export. It avoids serializing
/// the result once for the reshape bound and again for the caller.
pub fn reshape_to_csv(table: Table, recipe: &Recipe, limits: Limits) -> Result<ReshapedCsv, Error> {
    let output = transform::reshape(table, recipe, limits, &NeverCancelled)?;
    Ok(ReshapedCsv {
        rows: output.table.rows.len(),
        columns: output.table.headers.len(),
        bytes: output.csv,
    })
}

/// Reshapes like [`reshape`], polling a caller-owned cancellation source.
///
/// Cancellation is cooperative and returns no partial table.
pub fn reshape_with_cancellation(
    table: Table,
    recipe: &Recipe,
    limits: Limits,
    cancellation: &dyn Cancellation,
) -> Result<SourcedTable, Error> {
    Ok(transform::reshape(table, recipe, limits, cancellation)?.table)
}
