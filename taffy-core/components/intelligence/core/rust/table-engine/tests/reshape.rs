// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use table_engine::{reshape, reshape_to_csv, Error, Limits, Recipe, Table};

#[path = "reshape/bounds.rs"]
mod bounds;
#[path = "reshape/security.rs"]
mod security;

fn table(input: &str) -> Table {
    match Table::from_csv(input.as_bytes(), Limits::default()) {
        Ok(table) => table,
        Err(error) => unreachable!("fixture must parse: {error}"),
    }
}

fn recipe(input: &str) -> Recipe {
    match Recipe::from_json(input.as_bytes(), Limits::default()) {
        Ok(recipe) => recipe,
        Err(error) => unreachable!("fixture must parse: {error}"),
    }
}

fn csv(input: &str, recipe_json: &str) -> Result<String, Error> {
    let output = reshape_to_csv(table(input), &recipe(recipe_json), Limits::default())?;
    String::from_utf8(output.into_bytes()).map_err(|_| Error::InvalidUtf8)
}

#[test]
fn strict_csv_preserves_quoted_content_and_initial_lineage() {
    let parsed = table("name,note\r\nAda,\"one, two\"\r\nLin,\"line 1\r\nline 2\"\r\n");
    assert_eq!(parsed.headers(), &["name", "note"]);
    assert_eq!(parsed.rows().len(), 2);
    let Some(note) = parsed.rows().get(1).and_then(|row| row.get(1)) else {
        unreachable!("second note exists");
    };
    assert_eq!(note.value(), "line 1\r\nline 2");
    assert_eq!(note.lineage().len(), 1);
    let Some(source) = note.lineage().first() else {
        unreachable!("source exists");
    };
    assert_eq!((source.input_row, source.input_column), (2, 1));
}

#[test]
fn a_pipeline_filters_and_orders_exact_decimals_then_selects_and_limits() {
    let output = csv(
        "id,amount,city\r\na,2.10,Delhi\r\nb,10.01,Pune\r\nc,2.01,Delhi\r\nd,2.10,Pune\r\n",
        r#"{"operations":[
          {"op":"filter","column":"amount","predicate":"greater_than_or_equal","value":"2.1","mode":"decimal"},
          {"op":"order","by":[{"column":"amount","direction":"descending","mode":"decimal"},{"column":"id","direction":"ascending"}]},
          {"op":"dedupe","columns":["amount"],"keep":"first"},
          {"op":"select","columns":["id","amount"]},
          {"op":"limit","rows":2}
        ]}"#,
    );
    assert_eq!(output, Ok("id,amount\r\nb,10.01\r\na,2.10\r\n".to_owned()));
}

#[test]
fn grouping_uses_exact_decimal_arithmetic_and_unions_lineage() {
    let input = table("city,amount\r\nDelhi,0.1\r\nDelhi,0.2\r\nPune,-2.25\r\nPune,1.25\r\n");
    let output = reshape(
        input,
        &recipe(
            r#"{"operations":[{"op":"group","by":["city"],"aggregates":[
              {"column":"amount","as":"total","function":"sum","mode":"decimal"},
              {"column":"amount","as":"entries","function":"count"},
              {"column":"amount","as":"minimum","function":"minimum","mode":"decimal"}
            ]}]}"#,
        ),
        Limits::default(),
    );
    let Ok(output) = output else {
        unreachable!("grouping succeeds");
    };
    let encoded = output.to_csv(Limits::default());
    assert_eq!(
        encoded,
        Ok(b"city,total,entries,minimum\r\nDelhi,0.3,2,0.1\r\nPune,-1,2,-2.25\r\n".to_vec())
    );
    let Some(total) = output.rows().first().and_then(|row| row.get(1)) else {
        unreachable!("grouped total exists");
    };
    assert_eq!(total.lineage().len(), 2);
    assert_eq!(output.row_lineage(0), Some(vec![1, 2]));
    assert_eq!(output.row_lineage(1), Some(vec![3, 4]));
}

#[test]
fn pivot_columns_and_rows_are_sorted_and_missing_cells_are_explicit() {
    let output = csv(
        "region,quarter,amount\r\nwest,Q2,2\r\neast,Q1,3\r\nwest,Q1,1\r\neast,Q1,4\r\n",
        r#"{"operations":[{"op":"pivot","rows":["region"],"column":"quarter","value":"amount","aggregate":"sum","mode":"decimal"}]}"#,
    );
    assert_eq!(
        output,
        Ok("region,quarter=Q1,quarter=Q2\r\neast,7,\r\nwest,1,2\r\n".to_owned())
    );
}

#[test]
fn duplicate_retention_is_stable_in_both_directions() {
    let input = "key,value\r\na,first\r\nb,only\r\na,last\r\n";
    let first = csv(
        input,
        r#"{"operations":[{"op":"dedupe","columns":["key"],"keep":"first"}]}"#,
    );
    let last = csv(
        input,
        r#"{"operations":[{"op":"dedupe","columns":["key"],"keep":"last"}]}"#,
    );
    assert_eq!(first, Ok("key,value\r\na,first\r\nb,only\r\n".to_owned()));
    assert_eq!(last, Ok("key,value\r\nb,only\r\na,last\r\n".to_owned()));
}

#[test]
fn selecting_reordered_columns_moves_values_and_keeps_exact_lineage() {
    let output = reshape(
        table("first,middle,last\r\na,b,c\r\nd,e,f\r\n"),
        &recipe(r#"{"operations":[{"op":"select","columns":["last","first"]}]}"#),
        Limits::default(),
    );
    let Ok(output) = output else {
        unreachable!("the selection succeeds")
    };
    assert_eq!(
        output.to_csv(Limits::default()),
        Ok(b"last,first\r\nc,a\r\nf,d\r\n".to_vec())
    );
    let Some(row) = output.rows().first() else {
        unreachable!("the selected row exists")
    };
    assert_eq!(
        row.iter()
            .filter_map(|cell| cell.lineage().first())
            .map(|source| source.input_column)
            .collect::<Vec<_>>(),
        vec![2, 0]
    );
}

#[test]
fn the_same_input_and_recipe_are_byte_deterministic() {
    let input = "group,value\r\nb,1.20\r\na,2.30\r\nb,3.40\r\n";
    let recipe = r#"{"operations":[{"op":"group","by":["group"],"aggregates":[{"column":"value","as":"sum","function":"sum","mode":"decimal"}]}]}"#;
    let first = csv(input, recipe);
    let second = csv(input, recipe);
    assert_eq!(first, second);
}
