// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The narrow adapter from one transient model call to `table-engine`.
//!
//! This is not a worker and not a browser action. Both inputs are already
//! bounded typed model operands; the engine parses them into a closed recipe,
//! runs inside this sandboxed core process, and returns one transient result
//! to the next turn. No byte is journalled or written to ambient storage.

use table_engine::{reshape_to_csv, Error, Limits, Recipe, Table};
use task_engine::{ArgumentValue, LoopOutcome, ModelToolCall, MAX_ARGUMENT_VALUE_BYTES};

const TABLE_ROWS: &str = "rows";
const TABLE_RECIPE: &str = "recipe";

const fn limits() -> Limits {
    Limits {
        max_input_bytes: MAX_ARGUMENT_VALUE_BYTES,
        max_recipe_bytes: MAX_ARGUMENT_VALUE_BYTES,
        max_output_bytes: task_engine::MAX_LOOP_RESULT_BYTES - 128,
        max_rows: 512,
        max_columns: 64,
        max_cells: 8_192,
        max_cell_bytes: MAX_ARGUMENT_VALUE_BYTES,
        max_steps: 16,
        max_lineage_per_cell: 512,
        max_total_lineage: 32_768,
    }
}

pub(crate) fn reshape_call(call: &ModelToolCall) -> (LoopOutcome, Vec<String>) {
    match execute(call) {
        Ok((csv, rows, columns)) => (
            LoopOutcome::TableReshaped { rows, columns },
            vec![format!("reshaped {rows} rows and {columns} columns"), csv],
        ),
        Err(error) => (
            LoopOutcome::TableRefused,
            vec![format!("table reshape refused: {}", error.label())],
        ),
    }
}

fn execute(call: &ModelToolCall) -> Result<(String, u32, u32), Error> {
    if call.tool_name != task_engine::TABLE_RESHAPE_TOOL {
        return Err(Error::InvalidRecipe);
    }
    let rows = text_argument(call, TABLE_ROWS).ok_or(Error::InvalidRecipe)?;
    let recipe = text_argument(call, TABLE_RECIPE).ok_or(Error::InvalidRecipe)?;
    let limits = limits();
    let table = Table::from_csv(rows.as_bytes(), limits)?;
    let recipe = Recipe::from_json(recipe.as_bytes(), limits)?;
    let output = reshape_to_csv(table, &recipe, limits)?;
    let row_count = u32::try_from(output.rows()).map_err(|_| Error::TooManyRows)?;
    let column_count = u32::try_from(output.columns()).map_err(|_| Error::TooManyColumns)?;
    let csv = String::from_utf8(output.into_bytes()).map_err(|_| Error::InvalidUtf8)?;
    Ok((csv, row_count, column_count))
}

fn text_argument<'a>(call: &'a ModelToolCall, name: &str) -> Option<&'a str> {
    call.arguments.iter().find_map(|argument| {
        if argument.name != name {
            return None;
        }
        match &argument.value {
            ArgumentValue::Text(value) => Some(value.as_str()),
            ArgumentValue::Handle(_)
            | ArgumentValue::Address(_)
            | ArgumentValue::Count(_)
            | ArgumentValue::Flag(_)
            | ArgumentValue::Choice(_)
            | ArgumentValue::SuppliedValue(_) => None,
        }
    })
}

#[cfg(test)]
mod tests {
    use super::reshape_call;
    use task_engine::{ArgumentValue, LoopOutcome, ModelToolCall, SuppliedArgument};

    fn call(rows: &str, recipe: &str) -> ModelToolCall {
        ModelToolCall::new(
            task_engine::TABLE_RESHAPE_TOOL,
            vec![
                SuppliedArgument::new("rows", ArgumentValue::Text(rows.to_owned())),
                SuppliedArgument::new("recipe", ArgumentValue::Text(recipe.to_owned())),
            ],
        )
    }

    #[test]
    fn the_native_adapter_returns_exact_csv_without_a_worker_or_browser_effect() {
        let (outcome, result) = reshape_call(&call(
            "name,value\r\nb,2\r\na,1\r\n",
            r#"{"operations":[{"op":"order","by":[{"column":"value","direction":"ascending","mode":"decimal"}]}]}"#,
        ));
        assert_eq!(
            outcome,
            LoopOutcome::TableReshaped {
                rows: 2,
                columns: 2
            }
        );
        assert_eq!(result.len(), 2);
        assert_eq!(result[1], "name,value\r\na,1\r\nb,2\r\n");
    }

    #[test]
    fn malformed_csv_and_recipe_return_only_a_closed_refusal() {
        for call in [
            call("name\nvalue", r#"{"operations":[{"op":"limit","rows":1}]}"#),
            call("name\r\nvalue", r#"{"operations":[{"op":"unknown"}]}"#),
        ] {
            let (outcome, result) = reshape_call(&call);
            assert_eq!(outcome, LoopOutcome::TableRefused);
            assert_eq!(result.len(), 1);
            assert!(result[0].starts_with("table reshape refused: "));
            assert!(!result[0].contains("name"));
        }
    }
}
