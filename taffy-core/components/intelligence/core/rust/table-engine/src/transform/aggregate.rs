// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core::cmp::Ordering;
use std::collections::{BTreeMap, BTreeSet};

use crate::decimal::Decimal;
use crate::{
    Aggregate, AggregateFunction, Cancellation, CellRef, ComparisonMode, Error, Limits,
    SourcedCell, SourcedTable,
};

use super::{column_index, column_indices, ensure_cell_count, merge_lineage, poll};

#[derive(Clone, Debug)]
struct Selected {
    text: String,
    decimal: Option<Decimal>,
}

#[derive(Clone, Debug)]
struct Accumulator {
    function: AggregateFunction,
    mode: ComparisonMode,
    count: u64,
    sum: Decimal,
    selected: Option<Selected>,
    lineage: Vec<CellRef>,
}

#[derive(Clone, Copy, Debug)]
struct LineageBudget {
    used: usize,
    maximum: usize,
}

impl LineageBudget {
    const fn new(maximum: usize) -> Self {
        Self { used: 0, maximum }
    }

    fn charge(&mut self, count: usize) -> Result<(), Error> {
        self.used = self
            .used
            .checked_add(count)
            .filter(|used| *used <= self.maximum)
            .ok_or(Error::LineageTooLarge)?;
        Ok(())
    }

    fn merge(
        &mut self,
        destination: &mut Vec<CellRef>,
        source: &[CellRef],
        mut limits: Limits,
    ) -> Result<(), Error> {
        let remaining = self.maximum.saturating_sub(self.used);
        limits.max_lineage_per_cell = limits
            .max_lineage_per_cell
            .min(destination.len().saturating_add(remaining));
        let added = merge_lineage(destination, source, limits)?;
        self.charge(added)
    }
}

impl Accumulator {
    fn new(function: AggregateFunction, mode: ComparisonMode) -> Result<Self, Error> {
        if function == AggregateFunction::Sum && mode != ComparisonMode::Decimal {
            return Err(Error::InvalidRecipe);
        }
        Ok(Self {
            function,
            mode,
            count: 0,
            sum: Decimal::zero(),
            selected: None,
            lineage: Vec::new(),
        })
    }

    fn add(
        &mut self,
        cell: &SourcedCell,
        limits: Limits,
        lineage_budget: &mut LineageBudget,
    ) -> Result<(), Error> {
        lineage_budget.merge(&mut self.lineage, cell.lineage(), limits)?;
        self.count = self.count.checked_add(1).ok_or(Error::DecimalOverflow)?;
        match self.function {
            AggregateFunction::Count => Ok(()),
            AggregateFunction::Sum => {
                self.sum = self.sum.checked_add(Decimal::parse(cell.value())?)?;
                Ok(())
            }
            AggregateFunction::Minimum | AggregateFunction::Maximum => {
                let candidate = Selected {
                    text: cell.value().to_owned(),
                    decimal: if self.mode == ComparisonMode::Decimal {
                        Some(Decimal::parse(cell.value())?)
                    } else {
                        None
                    },
                };
                let replace = self.selected.as_ref().is_none_or(|selected| {
                    let ordering = compare_selected(&candidate, selected, self.mode);
                    match self.function {
                        AggregateFunction::Minimum => ordering == Ordering::Less,
                        AggregateFunction::Maximum => ordering == Ordering::Greater,
                        AggregateFunction::Count | AggregateFunction::Sum => false,
                    }
                });
                if replace {
                    self.selected = Some(candidate);
                }
                Ok(())
            }
        }
    }

    fn finish(self) -> Result<SourcedCell, Error> {
        let value = match self.function {
            AggregateFunction::Count => self.count.to_string(),
            AggregateFunction::Sum => self.sum.canonical(),
            AggregateFunction::Minimum | AggregateFunction::Maximum => {
                self.selected.ok_or(Error::InvalidRecipe)?.text
            }
        };
        Ok(SourcedCell::new(value, self.lineage))
    }
}

fn compare_selected(left: &Selected, right: &Selected, mode: ComparisonMode) -> Ordering {
    match mode {
        ComparisonMode::Text => left.text.cmp(&right.text),
        ComparisonMode::Decimal => match (left.decimal, right.decimal) {
            (Some(left), Some(right)) => left.cmp(&right),
            _ => Ordering::Equal,
        },
    }
}

#[derive(Clone, Debug)]
struct GroupBucket {
    keys: Vec<SourcedCell>,
    aggregates: Vec<Accumulator>,
}

pub(super) fn group(
    table: &SourcedTable,
    by: &[String],
    aggregates: &[Aggregate],
    limits: Limits,
    cancellation: &dyn Cancellation,
) -> Result<SourcedTable, Error> {
    let key_indices = column_indices(&table.headers, by)?;
    let aggregate_indices: Vec<usize> = aggregates
        .iter()
        .map(|aggregate| column_index(&table.headers, &aggregate.column))
        .collect::<Result<_, _>>()?;
    let mut lineage_budget = LineageBudget::new(limits.max_total_lineage);
    let mut groups = BTreeMap::<Vec<String>, GroupBucket>::new();
    for row in &table.rows {
        poll(cancellation)?;
        let key = values_at(row, &key_indices)?;
        if let Some(bucket) = groups.get_mut(&key) {
            merge_key_cells(
                &mut bucket.keys,
                row,
                &key_indices,
                limits,
                &mut lineage_budget,
            )?;
            add_aggregates(
                &mut bucket.aggregates,
                row,
                &aggregate_indices,
                limits,
                &mut lineage_budget,
            )?;
        } else {
            let keys = cells_at(row, &key_indices, &mut lineage_budget)?;
            let mut values = new_aggregates(aggregates)?;
            add_aggregates(
                &mut values,
                row,
                &aggregate_indices,
                limits,
                &mut lineage_budget,
            )?;
            groups.insert(
                key,
                GroupBucket {
                    keys,
                    aggregates: values,
                },
            );
        }
    }
    let mut headers = by.to_vec();
    headers.extend(aggregates.iter().map(|aggregate| aggregate.output.clone()));
    ensure_cell_count(groups.len(), headers.len(), limits)?;
    let mut rows = Vec::with_capacity(groups.len());
    for (_, bucket) in groups {
        poll(cancellation)?;
        let mut row = bucket.keys;
        for aggregate in bucket.aggregates {
            row.push(aggregate.finish()?);
        }
        rows.push(row);
    }
    Ok(SourcedTable { headers, rows })
}

fn new_aggregates(aggregates: &[Aggregate]) -> Result<Vec<Accumulator>, Error> {
    aggregates
        .iter()
        .map(|aggregate| Accumulator::new(aggregate.function, aggregate.mode))
        .collect()
}

fn add_aggregates(
    aggregates: &mut [Accumulator],
    row: &[SourcedCell],
    indices: &[usize],
    limits: Limits,
    lineage_budget: &mut LineageBudget,
) -> Result<(), Error> {
    for (aggregate, index) in aggregates.iter_mut().zip(indices) {
        aggregate.add(
            row.get(*index).ok_or(Error::TooManyCells)?,
            limits,
            lineage_budget,
        )?;
    }
    Ok(())
}

fn merge_key_cells(
    keys: &mut [SourcedCell],
    row: &[SourcedCell],
    indices: &[usize],
    limits: Limits,
    lineage_budget: &mut LineageBudget,
) -> Result<(), Error> {
    for (key, index) in keys.iter_mut().zip(indices) {
        let source = row.get(*index).ok_or(Error::TooManyCells)?;
        lineage_budget.merge(&mut key.lineage, source.lineage(), limits)?;
    }
    Ok(())
}

#[derive(Clone, Debug)]
struct PivotBucket {
    keys: Vec<SourcedCell>,
    values: BTreeMap<String, Accumulator>,
}

#[allow(clippy::too_many_arguments)]
pub(super) fn pivot(
    table: &SourcedTable,
    row_columns: &[String],
    pivot_column: &str,
    value_column: &str,
    function: AggregateFunction,
    mode: ComparisonMode,
    limits: Limits,
    cancellation: &dyn Cancellation,
) -> Result<SourcedTable, Error> {
    let row_indices = column_indices(&table.headers, row_columns)?;
    let pivot_index = column_index(&table.headers, pivot_column)?;
    let value_index = column_index(&table.headers, value_column)?;
    let mut lineage_budget = LineageBudget::new(limits.max_total_lineage);
    let mut pivot_values = BTreeSet::<String>::new();
    let mut groups = BTreeMap::<Vec<String>, PivotBucket>::new();
    for row in &table.rows {
        poll(cancellation)?;
        let key = values_at(row, &row_indices)?;
        let pivot_value = row
            .get(pivot_index)
            .ok_or(Error::TooManyCells)?
            .value()
            .to_owned();
        if pivot_values.insert(pivot_value.clone())
            && row_columns.len().saturating_add(pivot_values.len()) > limits.max_columns
        {
            return Err(Error::TooManyColumns);
        }
        let value = row.get(value_index).ok_or(Error::TooManyCells)?;
        if let Some(bucket) = groups.get_mut(&key) {
            merge_key_cells(
                &mut bucket.keys,
                row,
                &row_indices,
                limits,
                &mut lineage_budget,
            )?;
            if let Some(accumulator) = bucket.values.get_mut(&pivot_value) {
                accumulator.add(value, limits, &mut lineage_budget)?;
            } else {
                let mut accumulator = Accumulator::new(function, mode)?;
                accumulator.add(value, limits, &mut lineage_budget)?;
                bucket.values.insert(pivot_value, accumulator);
            }
        } else {
            let mut accumulator = Accumulator::new(function, mode)?;
            accumulator.add(value, limits, &mut lineage_budget)?;
            groups.insert(
                key,
                PivotBucket {
                    keys: cells_at(row, &row_indices, &mut lineage_budget)?,
                    values: BTreeMap::from([(pivot_value, accumulator)]),
                },
            );
        }
    }
    let mut headers = row_columns.to_vec();
    for value in &pivot_values {
        let header = format!("{pivot_column}={value}");
        if header.len() > limits.max_cell_bytes {
            return Err(Error::CellTooLarge);
        }
        headers.push(header);
    }
    ensure_cell_count(groups.len(), headers.len(), limits)?;
    let mut rows = Vec::with_capacity(groups.len());
    for (_, mut bucket) in groups {
        poll(cancellation)?;
        let mut row = bucket.keys;
        for pivot_value in &pivot_values {
            if let Some(accumulator) = bucket.values.remove(pivot_value) {
                row.push(accumulator.finish()?);
            } else {
                row.push(SourcedCell::new(String::new(), Vec::new()));
            }
        }
        rows.push(row);
    }
    Ok(SourcedTable { headers, rows })
}

fn values_at(row: &[SourcedCell], indices: &[usize]) -> Result<Vec<String>, Error> {
    indices
        .iter()
        .map(|index| {
            row.get(*index)
                .map(|cell| cell.value().to_owned())
                .ok_or(Error::TooManyCells)
        })
        .collect()
}

fn cells_at(
    row: &[SourcedCell],
    indices: &[usize],
    lineage_budget: &mut LineageBudget,
) -> Result<Vec<SourcedCell>, Error> {
    let mut cells = Vec::with_capacity(indices.len());
    for index in indices {
        let cell = row.get(*index).ok_or(Error::TooManyCells)?;
        lineage_budget.charge(cell.lineage().len())?;
        cells.push(cell.clone());
    }
    Ok(cells)
}
