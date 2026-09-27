// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::{csv, Error, Limits};

/// One source cell in the original comma-separated input.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct CellRef {
    /// One-based data-row number. The header is row zero and is not a cell source.
    pub input_row: u32,
    /// Zero-based input-column number.
    pub input_column: u32,
}

/// One cell and the exact bounded input cells that contributed to it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SourcedCell {
    pub(crate) value: String,
    pub(crate) lineage: Vec<CellRef>,
}

impl SourcedCell {
    pub(crate) fn new(value: String, lineage: Vec<CellRef>) -> Self {
        Self { value, lineage }
    }

    /// The cell's semantic text, before spreadsheet export hardening.
    pub fn value(&self) -> &str {
        &self.value
    }

    /// Ordered, de-duplicated original input cells that contributed to it.
    pub fn lineage(&self) -> &[CellRef] {
        &self.lineage
    }
}

/// One parsed table. Construction is strict RFC 4180 and always bounded.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Table {
    pub(crate) headers: Vec<String>,
    pub(crate) rows: Vec<Vec<SourcedCell>>,
}

impl Table {
    /// Parses one UTF-8 RFC 4180 table with a non-empty, unique header row.
    pub fn from_csv(input: &[u8], limits: Limits) -> Result<Self, Error> {
        csv::parse(input, limits)
    }

    /// Column names in order.
    pub fn headers(&self) -> &[String] {
        &self.headers
    }

    /// Data rows in order.
    pub fn rows(&self) -> &[Vec<SourcedCell>] {
        &self.rows
    }

    /// Ordered original input rows that contributed to one current row.
    pub fn row_lineage(&self, row_index: usize) -> Option<Vec<u32>> {
        self.rows.get(row_index).map(|row| row_lineage(row))
    }
}

/// A reshaped table with bounded source lineage on every output cell.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SourcedTable {
    pub(crate) headers: Vec<String>,
    pub(crate) rows: Vec<Vec<SourcedCell>>,
}

impl SourcedTable {
    /// Column names in output order.
    pub fn headers(&self) -> &[String] {
        &self.headers
    }

    /// Output rows in deterministic order.
    pub fn rows(&self) -> &[Vec<SourcedCell>] {
        &self.rows
    }

    /// Ordered original input rows that contributed to one output row.
    ///
    /// This is derived from the bounded cell lineage, so it cannot disagree
    /// with the provenance exposed for the row's individual cells.
    pub fn row_lineage(&self, row_index: usize) -> Option<Vec<u32>> {
        self.rows.get(row_index).map(|row| row_lineage(row))
    }

    /// Encodes strict RFC 4180 bytes, neutralizing spreadsheet formulas.
    pub fn to_csv(&self, limits: Limits) -> Result<Vec<u8>, Error> {
        csv::write(self, limits)
    }
}

fn row_lineage(row: &[SourcedCell]) -> Vec<u32> {
    let mut rows: Vec<u32> = row
        .iter()
        .flat_map(SourcedCell::lineage)
        .map(|source| source.input_row)
        .collect();
    rows.sort_unstable();
    rows.dedup();
    rows
}
