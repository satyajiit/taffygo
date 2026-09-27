// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod aggregate;
mod filter;
mod order;

use std::collections::{BTreeMap, BTreeSet};

use crate::{Cancellation, Error, Keep, Limits, Recipe, SourcedCell, SourcedTable, Step, Table};

pub(crate) struct Output {
    pub(crate) table: SourcedTable,
    pub(crate) csv: Vec<u8>,
}

pub(crate) fn reshape(
    table: Table,
    recipe: &Recipe,
    limits: Limits,
    cancellation: &dyn Cancellation,
) -> Result<Output, Error> {
    if recipe.operations().len() > limits.max_steps {
        return Err(Error::TooManySteps);
    }
    let mut table = SourcedTable {
        headers: table.headers,
        rows: table.rows,
    };
    check_shape(&table, limits)?;
    for operation in recipe.operations() {
        poll(cancellation)?;
        table = match operation {
            Step::Select { columns } => select(table, columns)?,
            Step::Order { by } => order::apply(table, by, cancellation)?,
            Step::Filter {
                column,
                predicate,
                value,
                mode,
            } => filter::apply(
                table,
                column,
                *predicate,
                value.as_deref(),
                *mode,
                cancellation,
            )?,
            Step::Group { by, aggregates } => {
                aggregate::group(&table, by, aggregates, limits, cancellation)?
            }
            Step::Pivot {
                rows,
                column,
                value,
                aggregate,
                mode,
            } => aggregate::pivot(
                &table,
                rows,
                column,
                value,
                *aggregate,
                *mode,
                limits,
                cancellation,
            )?,
            Step::Dedupe { columns, keep } => dedupe(table, columns, *keep, cancellation)?,
            Step::Limit { rows } => limit(table, *rows),
        };
        check_shape(&table, limits)?;
    }
    // The output-byte bound is part of reshape, not deferred to whichever
    // caller happens to serialize it. Keep the bytes so a caller that needs
    // the CSV does not repeat the full validation and encoding pass.
    let csv = table.to_csv(limits)?;
    Ok(Output { table, csv })
}

pub(super) fn poll(cancellation: &dyn Cancellation) -> Result<(), Error> {
    if cancellation.is_cancelled() {
        Err(Error::Cancelled)
    } else {
        Ok(())
    }
}

pub(super) fn column_index(headers: &[String], name: &str) -> Result<usize, Error> {
    headers
        .iter()
        .position(|header| header == name)
        .ok_or(Error::UnknownColumn)
}

pub(super) fn column_indices(headers: &[String], names: &[String]) -> Result<Vec<usize>, Error> {
    let mut indices = Vec::with_capacity(names.len());
    for name in names {
        let index = column_index(headers, name)?;
        if indices.contains(&index) {
            return Err(Error::DuplicateColumn);
        }
        indices.push(index);
    }
    Ok(indices)
}

fn select(table: SourcedTable, columns: &[String]) -> Result<SourcedTable, Error> {
    let indices = column_indices(&table.headers, columns)?;
    let mut source_order = indices.clone();
    source_order.sort_unstable();
    let mut target_by_source_order = Vec::with_capacity(source_order.len());
    for source_index in &source_order {
        let target = indices
            .iter()
            .position(|candidate| candidate == source_index)
            .ok_or(Error::UnknownColumn)?;
        target_by_source_order.push(target);
    }
    let mut current_targets = target_by_source_order.clone();
    let mut rows = Vec::with_capacity(table.rows.len());
    for row in table.rows {
        let mut selected = Vec::with_capacity(source_order.len());
        let mut wanted = source_order.iter().copied();
        let mut next_wanted = wanted.next();
        for (source_index, cell) in row.into_iter().enumerate() {
            if next_wanted == Some(source_index) {
                selected.push(cell);
                next_wanted = wanted.next();
            }
        }
        if next_wanted.is_some() || selected.len() != source_order.len() {
            return Err(Error::TooManyCells);
        }
        current_targets.copy_from_slice(&target_by_source_order);
        for position in 0..current_targets.len() {
            loop {
                let target = current_targets
                    .get(position)
                    .copied()
                    .ok_or(Error::TooManyColumns)?;
                if target == position {
                    break;
                }
                if target >= current_targets.len() {
                    return Err(Error::TooManyColumns);
                }
                selected.swap(position, target);
                current_targets.swap(position, target);
            }
        }
        rows.push(selected);
    }
    Ok(SourcedTable {
        headers: columns.to_vec(),
        rows,
    })
}

fn dedupe(
    table: SourcedTable,
    columns: &[String],
    keep: Keep,
    cancellation: &dyn Cancellation,
) -> Result<SourcedTable, Error> {
    let indices = column_indices(&table.headers, columns)?;
    let mut retained = vec![false; table.rows.len()];
    match keep {
        Keep::First => {
            let mut seen = BTreeSet::new();
            for (index, row) in table.rows.iter().enumerate() {
                poll(cancellation)?;
                if seen.insert(borrowed_key(row, &indices)?) {
                    let slot = retained.get_mut(index).ok_or(Error::TooManyRows)?;
                    *slot = true;
                }
            }
        }
        Keep::Last => {
            let mut last = BTreeMap::new();
            for (index, row) in table.rows.iter().enumerate() {
                poll(cancellation)?;
                last.insert(borrowed_key(row, &indices)?, index);
            }
            for index in last.into_values() {
                let slot = retained.get_mut(index).ok_or(Error::TooManyRows)?;
                *slot = true;
            }
        }
    }
    let mut rows = Vec::with_capacity(retained.iter().filter(|keep| **keep).count());
    for (row, keep) in table.rows.into_iter().zip(retained) {
        poll(cancellation)?;
        if keep {
            rows.push(row);
        }
    }
    Ok(SourcedTable {
        headers: table.headers,
        rows,
    })
}

fn borrowed_key<'a>(row: &'a [SourcedCell], indices: &[usize]) -> Result<Vec<&'a str>, Error> {
    indices
        .iter()
        .map(|index| {
            row.get(*index)
                .map(SourcedCell::value)
                .ok_or(Error::TooManyCells)
        })
        .collect()
}

fn limit(mut table: SourcedTable, rows: usize) -> SourcedTable {
    table.rows.truncate(rows);
    table
}

pub(super) fn merge_lineage(
    destination: &mut Vec<crate::CellRef>,
    source: &[crate::CellRef],
    limits: Limits,
) -> Result<usize, Error> {
    let original_len = destination.len();
    let mut left = destination.iter().copied().peekable();
    let mut right = source.iter().copied().peekable();
    let mut merged = Vec::with_capacity(
        destination
            .len()
            .saturating_add(source.len())
            .min(limits.max_lineage_per_cell),
    );
    while left.peek().is_some() || right.peek().is_some() {
        let next = match (left.peek().copied(), right.peek().copied()) {
            (Some(left_item), Some(right_item)) if left_item <= right_item => {
                let _ = left.next();
                left_item
            }
            (_, Some(right_item)) => {
                let _ = right.next();
                right_item
            }
            (Some(left_item), None) => {
                let _ = left.next();
                left_item
            }
            (None, None) => break,
        };
        if merged.last() != Some(&next) {
            if merged.len() >= limits.max_lineage_per_cell {
                return Err(Error::LineageTooLarge);
            }
            merged.push(next);
        }
    }
    *destination = merged;
    Ok(destination.len().saturating_sub(original_len))
}

fn check_shape(table: &SourcedTable, limits: Limits) -> Result<(), Error> {
    if table.headers.is_empty() || table.headers.len() > limits.max_columns {
        return Err(Error::TooManyColumns);
    }
    for (index, header) in table.headers.iter().enumerate() {
        if header.is_empty() || header.len() > limits.max_cell_bytes {
            return Err(Error::CellTooLarge);
        }
        if table
            .headers
            .iter()
            .take(index)
            .any(|prior| prior == header)
        {
            return Err(Error::DuplicateColumn);
        }
    }
    if table.rows.len() > limits.max_rows {
        return Err(Error::TooManyRows);
    }
    ensure_cell_count(table.rows.len(), table.headers.len(), limits)?;
    let mut total_lineage = 0_usize;
    for row in &table.rows {
        if row.len() != table.headers.len() {
            return Err(Error::TooManyCells);
        }
        for cell in row {
            if cell.value().len() > limits.max_cell_bytes {
                return Err(Error::CellTooLarge);
            }
            if cell.lineage().len() > limits.max_lineage_per_cell {
                return Err(Error::LineageTooLarge);
            }
            total_lineage = total_lineage
                .checked_add(cell.lineage().len())
                .ok_or(Error::LineageTooLarge)?;
            if total_lineage > limits.max_total_lineage {
                return Err(Error::LineageTooLarge);
            }
        }
    }
    Ok(())
}

pub(super) fn ensure_cell_count(rows: usize, columns: usize, limits: Limits) -> Result<(), Error> {
    let cells = rows.checked_mul(columns).ok_or(Error::TooManyCells)?;
    if cells > limits.max_cells {
        Err(Error::TooManyCells)
    } else {
        Ok(())
    }
}
