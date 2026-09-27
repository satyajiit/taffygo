// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core::cmp::Ordering;

use crate::decimal::Decimal;
use crate::{Cancellation, ComparisonMode, Direction, Error, OrderKey, SourcedTable};

use super::{column_index, poll};

#[derive(Clone, Debug, PartialEq, Eq)]
enum SortValue {
    Text(String),
    Decimal(Decimal),
}

impl Ord for SortValue {
    fn cmp(&self, other: &Self) -> Ordering {
        match (self, other) {
            (Self::Text(left), Self::Text(right)) => left.cmp(right),
            (Self::Decimal(left), Self::Decimal(right)) => left.cmp(right),
            // Construction uses one mode per key, so mixed values are not
            // reachable. Keeping the order total avoids a hidden panic.
            (Self::Text(_), Self::Decimal(_)) => Ordering::Less,
            (Self::Decimal(_), Self::Text(_)) => Ordering::Greater,
        }
    }
}

impl PartialOrd for SortValue {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

pub(super) fn apply(
    table: SourcedTable,
    keys: &[OrderKey],
    cancellation: &dyn Cancellation,
) -> Result<SourcedTable, Error> {
    let resolved: Vec<(usize, Direction, ComparisonMode)> = keys
        .iter()
        .map(|key| {
            column_index(&table.headers, &key.column).map(|index| (index, key.direction, key.mode))
        })
        .collect::<Result<_, _>>()?;
    let mut decorated = Vec::with_capacity(table.rows.len());
    for (ordinal, row) in table.rows.into_iter().enumerate() {
        poll(cancellation)?;
        let mut values = Vec::with_capacity(resolved.len());
        for (index, _, mode) in &resolved {
            let cell = row.get(*index).ok_or(Error::TooManyCells)?;
            values.push(match mode {
                ComparisonMode::Text => SortValue::Text(cell.value().to_owned()),
                ComparisonMode::Decimal => SortValue::Decimal(Decimal::parse(cell.value())?),
            });
        }
        decorated.push((values, ordinal, row));
    }
    decorated.sort_by(|left, right| {
        for (key_index, (_, direction, _)) in resolved.iter().enumerate() {
            let ordering = match (left.0.get(key_index), right.0.get(key_index)) {
                (Some(left), Some(right)) => left.cmp(right),
                _ => Ordering::Equal,
            };
            if ordering != Ordering::Equal {
                return match direction {
                    Direction::Ascending => ordering,
                    Direction::Descending => ordering.reverse(),
                };
            }
        }
        left.1.cmp(&right.1)
    });
    Ok(SourcedTable {
        headers: table.headers,
        rows: decorated.into_iter().map(|(_, _, row)| row).collect(),
    })
}
