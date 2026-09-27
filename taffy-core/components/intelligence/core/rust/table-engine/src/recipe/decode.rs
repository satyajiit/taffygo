// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::BTreeMap;

use crate::json::Value;
use crate::Error;

use super::{
    Aggregate, AggregateFunction, ComparisonMode, Direction, Keep, OrderKey, Predicate, Recipe,
    Step,
};

pub(super) fn recipe(value: Value) -> Result<Recipe, Error> {
    let mut root = object(value)?;
    let operations = array(take(&mut root, "operations")?)?
        .into_iter()
        .map(step)
        .collect::<Result<Vec<_>, _>>()?;
    require_empty(&root)?;
    Ok(Recipe { operations })
}

fn step(value: Value) -> Result<Step, Error> {
    let mut fields = object(value)?;
    let operation = text(take(&mut fields, "op")?)?;
    let step = match operation.as_str() {
        "select" => Step::Select {
            columns: strings(take(&mut fields, "columns")?)?,
        },
        "order" => Step::Order {
            by: array(take(&mut fields, "by")?)?
                .into_iter()
                .map(order_key)
                .collect::<Result<_, _>>()?,
        },
        "filter" => Step::Filter {
            column: text(take(&mut fields, "column")?)?,
            predicate: predicate(&text(take(&mut fields, "predicate")?)?)?,
            value: optional_text(&mut fields, "value")?,
            mode: optional_mode(&mut fields)?,
        },
        "group" => Step::Group {
            by: strings(take(&mut fields, "by")?)?,
            aggregates: array(take(&mut fields, "aggregates")?)?
                .into_iter()
                .map(aggregate)
                .collect::<Result<_, _>>()?,
        },
        "pivot" => Step::Pivot {
            rows: strings(take(&mut fields, "rows")?)?,
            column: text(take(&mut fields, "column")?)?,
            value: text(take(&mut fields, "value")?)?,
            aggregate: aggregate_function(&text(take(&mut fields, "aggregate")?)?)?,
            mode: optional_mode(&mut fields)?,
        },
        "dedupe" => Step::Dedupe {
            columns: strings(take(&mut fields, "columns")?)?,
            keep: keep(&text(take(&mut fields, "keep")?)?)?,
        },
        "limit" => Step::Limit {
            rows: usize::try_from(unsigned(&take(&mut fields, "rows")?)?)
                .map_err(|_| Error::TooManyRows)?,
        },
        _ => return Err(Error::InvalidRecipe),
    };
    require_empty(&fields)?;
    Ok(step)
}

fn order_key(value: Value) -> Result<OrderKey, Error> {
    let mut fields = object(value)?;
    let key = OrderKey {
        column: text(take(&mut fields, "column")?)?,
        direction: direction(&text(take(&mut fields, "direction")?)?)?,
        mode: optional_mode(&mut fields)?,
    };
    require_empty(&fields)?;
    Ok(key)
}

fn aggregate(value: Value) -> Result<Aggregate, Error> {
    let mut fields = object(value)?;
    let aggregate = Aggregate {
        column: text(take(&mut fields, "column")?)?,
        output: text(take(&mut fields, "as")?)?,
        function: aggregate_function(&text(take(&mut fields, "function")?)?)?,
        mode: optional_mode(&mut fields)?,
    };
    require_empty(&fields)?;
    Ok(aggregate)
}

fn optional_mode(fields: &mut BTreeMap<String, Value>) -> Result<ComparisonMode, Error> {
    fields
        .remove("mode")
        .map(text)
        .transpose()?
        .as_deref()
        .map(mode)
        .transpose()
        .map(Option::unwrap_or_default)
}

fn optional_text(
    fields: &mut BTreeMap<String, Value>,
    name: &str,
) -> Result<Option<String>, Error> {
    fields.remove(name).map(text).transpose()
}

fn object(value: Value) -> Result<BTreeMap<String, Value>, Error> {
    match value {
        Value::Object(value) => Ok(value),
        Value::Array(_) | Value::Text(_) | Value::Unsigned(_) | Value::Other => {
            Err(Error::InvalidRecipe)
        }
    }
}

fn array(value: Value) -> Result<Vec<Value>, Error> {
    match value {
        Value::Array(value) => Ok(value),
        Value::Object(_) | Value::Text(_) | Value::Unsigned(_) | Value::Other => {
            Err(Error::InvalidRecipe)
        }
    }
}

fn text(value: Value) -> Result<String, Error> {
    match value {
        Value::Text(value) => Ok(value),
        Value::Object(_) | Value::Array(_) | Value::Unsigned(_) | Value::Other => {
            Err(Error::InvalidRecipe)
        }
    }
}

fn unsigned(value: &Value) -> Result<u64, Error> {
    match value {
        Value::Unsigned(value) => Ok(*value),
        Value::Object(_) | Value::Array(_) | Value::Text(_) | Value::Other => {
            Err(Error::InvalidRecipe)
        }
    }
}

fn strings(value: Value) -> Result<Vec<String>, Error> {
    array(value)?
        .into_iter()
        .map(text)
        .collect::<Result<_, _>>()
}

fn take(fields: &mut BTreeMap<String, Value>, name: &str) -> Result<Value, Error> {
    fields.remove(name).ok_or(Error::InvalidRecipe)
}

fn require_empty(fields: &BTreeMap<String, Value>) -> Result<(), Error> {
    if fields.is_empty() {
        Ok(())
    } else {
        Err(Error::InvalidRecipe)
    }
}

fn mode(value: &str) -> Result<ComparisonMode, Error> {
    match value {
        "text" => Ok(ComparisonMode::Text),
        "decimal" => Ok(ComparisonMode::Decimal),
        _ => Err(Error::InvalidRecipe),
    }
}

fn direction(value: &str) -> Result<Direction, Error> {
    match value {
        "ascending" => Ok(Direction::Ascending),
        "descending" => Ok(Direction::Descending),
        _ => Err(Error::InvalidRecipe),
    }
}

fn predicate(value: &str) -> Result<Predicate, Error> {
    match value {
        "equal" => Ok(Predicate::Equal),
        "not_equal" => Ok(Predicate::NotEqual),
        "less_than" => Ok(Predicate::LessThan),
        "less_than_or_equal" => Ok(Predicate::LessThanOrEqual),
        "greater_than" => Ok(Predicate::GreaterThan),
        "greater_than_or_equal" => Ok(Predicate::GreaterThanOrEqual),
        "contains" => Ok(Predicate::Contains),
        "starts_with" => Ok(Predicate::StartsWith),
        "is_empty" => Ok(Predicate::IsEmpty),
        "is_not_empty" => Ok(Predicate::IsNotEmpty),
        _ => Err(Error::InvalidRecipe),
    }
}

fn aggregate_function(value: &str) -> Result<AggregateFunction, Error> {
    match value {
        "count" => Ok(AggregateFunction::Count),
        "sum" => Ok(AggregateFunction::Sum),
        "minimum" => Ok(AggregateFunction::Minimum),
        "maximum" => Ok(AggregateFunction::Maximum),
        _ => Err(Error::InvalidRecipe),
    }
}

fn keep(value: &str) -> Result<Keep, Error> {
    match value {
        "first" => Ok(Keep::First),
        "last" => Ok(Keep::Last),
        _ => Err(Error::InvalidRecipe),
    }
}
