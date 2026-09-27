// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::{Error, Limits};

mod decode;

/// One closed sequence of table operations.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Recipe {
    operations: Vec<Step>,
}

impl Recipe {
    /// Constructs a recipe from typed operations and checks every bound.
    pub fn new(operations: Vec<Step>, limits: Limits) -> Result<Self, Error> {
        let recipe = Self { operations };
        recipe.validate(limits)?;
        Ok(recipe)
    }

    /// Parses strict JSON into the closed recipe vocabulary.
    pub fn from_json(input: &[u8], limits: Limits) -> Result<Self, Error> {
        if input.len() > limits.max_recipe_bytes {
            return Err(Error::RecipeTooLarge);
        }
        let recipe = decode::recipe(crate::json::parse(input)?)?;
        recipe.validate(limits)?;
        Ok(recipe)
    }

    /// Operations in execution order.
    pub fn operations(&self) -> &[Step] {
        &self.operations
    }

    fn validate(&self, limits: Limits) -> Result<(), Error> {
        if self.operations.is_empty() {
            return Err(Error::InvalidRecipe);
        }
        if self.operations.len() > limits.max_steps {
            return Err(Error::TooManySteps);
        }
        for operation in &self.operations {
            operation.validate(limits)?;
        }
        Ok(())
    }
}

/// One deterministic transformation step.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Step {
    Select {
        columns: Vec<String>,
    },
    Order {
        by: Vec<OrderKey>,
    },
    Filter {
        column: String,
        predicate: Predicate,
        value: Option<String>,
        mode: ComparisonMode,
    },
    Group {
        by: Vec<String>,
        aggregates: Vec<Aggregate>,
    },
    Pivot {
        rows: Vec<String>,
        column: String,
        value: String,
        aggregate: AggregateFunction,
        mode: ComparisonMode,
    },
    Dedupe {
        columns: Vec<String>,
        keep: Keep,
    },
    Limit {
        rows: usize,
    },
}

impl Step {
    fn validate(&self, limits: Limits) -> Result<(), Error> {
        match self {
            Self::Select { columns } | Self::Dedupe { columns, .. } => {
                validate_names(columns, limits)
            }
            Self::Order { by } => {
                if by.is_empty() || by.len() > limits.max_columns {
                    return Err(Error::InvalidRecipe);
                }
                for (index, key) in by.iter().enumerate() {
                    validate_name(&key.column, limits)?;
                    if by
                        .iter()
                        .take(index)
                        .any(|prior| prior.column == key.column)
                    {
                        return Err(Error::DuplicateColumn);
                    }
                }
                Ok(())
            }
            Self::Filter {
                column,
                predicate,
                value,
                mode,
            } => {
                validate_name(column, limits)?;
                let needs_value = !matches!(predicate, Predicate::IsEmpty | Predicate::IsNotEmpty);
                if needs_value != value.is_some() {
                    return Err(Error::InvalidRecipe);
                }
                if *mode == ComparisonMode::Decimal
                    && matches!(predicate, Predicate::Contains | Predicate::StartsWith)
                {
                    return Err(Error::InvalidRecipe);
                }
                if let Some(value) = value {
                    validate_value(value, limits)?;
                }
                Ok(())
            }
            Self::Group { by, aggregates } => {
                validate_names(by, limits)?;
                if aggregates.is_empty() || aggregates.len() > limits.max_columns {
                    return Err(Error::InvalidRecipe);
                }
                for (index, aggregate) in aggregates.iter().enumerate() {
                    validate_name(&aggregate.column, limits)?;
                    validate_name(&aggregate.output, limits)?;
                    if aggregate.function == AggregateFunction::Sum
                        && aggregate.mode != ComparisonMode::Decimal
                    {
                        return Err(Error::InvalidRecipe);
                    }
                    if by.contains(&aggregate.output)
                        || aggregates
                            .iter()
                            .take(index)
                            .any(|prior| prior.output == aggregate.output)
                    {
                        return Err(Error::DuplicateColumn);
                    }
                }
                if by.len().saturating_add(aggregates.len()) > limits.max_columns {
                    return Err(Error::TooManyColumns);
                }
                Ok(())
            }
            Self::Pivot {
                rows,
                column,
                value,
                aggregate,
                mode,
            } => {
                validate_names(rows, limits)?;
                validate_name(column, limits)?;
                validate_name(value, limits)?;
                if *aggregate == AggregateFunction::Sum && *mode != ComparisonMode::Decimal {
                    Err(Error::InvalidRecipe)
                } else {
                    Ok(())
                }
            }
            Self::Limit { rows } => {
                if *rows > limits.max_rows {
                    Err(Error::TooManyRows)
                } else {
                    Ok(())
                }
            }
        }
    }
}

/// One stable sort key.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OrderKey {
    pub column: String,
    pub direction: Direction,
    pub mode: ComparisonMode,
}

/// Sort direction.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Direction {
    Ascending,
    Descending,
}

/// Whether comparisons use exact text or exact base-ten numbers.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ComparisonMode {
    #[default]
    Text,
    Decimal,
}

/// A closed filter predicate.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Predicate {
    Equal,
    NotEqual,
    LessThan,
    LessThanOrEqual,
    GreaterThan,
    GreaterThanOrEqual,
    Contains,
    StartsWith,
    IsEmpty,
    IsNotEmpty,
}

/// One named grouped result column.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Aggregate {
    pub column: String,
    pub output: String,
    pub function: AggregateFunction,
    pub mode: ComparisonMode,
}

/// Closed aggregation vocabulary.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AggregateFunction {
    Count,
    Sum,
    Minimum,
    Maximum,
}

/// Which duplicate survives a de-duplication step.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Keep {
    First,
    Last,
}

fn validate_names(names: &[String], limits: Limits) -> Result<(), Error> {
    if names.is_empty() || names.len() > limits.max_columns {
        return Err(Error::InvalidRecipe);
    }
    for (index, name) in names.iter().enumerate() {
        validate_name(name, limits)?;
        if names.iter().take(index).any(|prior| prior == name) {
            return Err(Error::DuplicateColumn);
        }
    }
    Ok(())
}

fn validate_name(name: &str, limits: Limits) -> Result<(), Error> {
    if name.is_empty()
        || name.len() > limits.max_cell_bytes
        || name.bytes().any(is_disallowed_control)
    {
        Err(Error::InvalidRecipe)
    } else {
        Ok(())
    }
}

fn validate_value(value: &str, limits: Limits) -> Result<(), Error> {
    if value.len() > limits.max_cell_bytes || value.bytes().any(is_disallowed_control) {
        Err(Error::InvalidRecipe)
    } else {
        Ok(())
    }
}

const fn is_disallowed_control(byte: u8) -> bool {
    matches!(byte, 0..=9 | 11..=12 | 14..=31 | 127)
}
