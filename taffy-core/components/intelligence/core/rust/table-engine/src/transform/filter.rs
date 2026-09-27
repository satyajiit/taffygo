// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core::cmp::Ordering;

use crate::decimal::Decimal;
use crate::{Cancellation, ComparisonMode, Error, Predicate, SourcedTable};

use super::{column_index, poll};

pub(super) fn apply(
    table: SourcedTable,
    column: &str,
    predicate: Predicate,
    operand: Option<&str>,
    mode: ComparisonMode,
    cancellation: &dyn Cancellation,
) -> Result<SourcedTable, Error> {
    let index = column_index(&table.headers, column)?;
    if mode == ComparisonMode::Decimal
        && matches!(predicate, Predicate::Contains | Predicate::StartsWith)
    {
        return Err(Error::InvalidRecipe);
    }
    let decimal_operand = if mode == ComparisonMode::Decimal {
        operand.map(Decimal::parse).transpose()?
    } else {
        None
    };
    let mut rows = Vec::new();
    for row in table.rows {
        poll(cancellation)?;
        let value = row.get(index).ok_or(Error::TooManyCells)?.value();
        if matches(value, predicate, operand, mode, decimal_operand)? {
            rows.push(row);
        }
    }
    Ok(SourcedTable {
        headers: table.headers,
        rows,
    })
}

fn matches(
    value: &str,
    predicate: Predicate,
    operand: Option<&str>,
    mode: ComparisonMode,
    decimal_operand: Option<Decimal>,
) -> Result<bool, Error> {
    if predicate == Predicate::IsEmpty {
        return Ok(value.is_empty());
    }
    if predicate == Predicate::IsNotEmpty {
        return Ok(!value.is_empty());
    }
    let operand = operand.ok_or(Error::InvalidRecipe)?;
    if predicate == Predicate::Contains {
        return Ok(value.contains(operand));
    }
    if predicate == Predicate::StartsWith {
        return Ok(value.starts_with(operand));
    }
    let ordering = match mode {
        ComparisonMode::Text => value.cmp(operand),
        ComparisonMode::Decimal => {
            Decimal::parse(value)?.cmp(&decimal_operand.ok_or(Error::InvalidRecipe)?)
        }
    };
    Ok(match predicate {
        Predicate::Equal => ordering == Ordering::Equal,
        Predicate::NotEqual => ordering != Ordering::Equal,
        Predicate::LessThan => ordering == Ordering::Less,
        Predicate::LessThanOrEqual => ordering != Ordering::Greater,
        Predicate::GreaterThan => ordering == Ordering::Greater,
        Predicate::GreaterThanOrEqual => ordering != Ordering::Less,
        Predicate::Contains
        | Predicate::StartsWith
        | Predicate::IsEmpty
        | Predicate::IsNotEmpty => false,
    })
}
