// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::cell::Cell;

use table_engine::{
    reshape, reshape_with_cancellation, Cancellation, Error, Limits, Recipe, Table,
};

use super::{csv, recipe, table};

#[test]
fn parsing_resource_ceilings_fail_closed() {
    let limits = Limits {
        max_input_bytes: 2,
        ..Limits::default()
    };
    assert_eq!(
        Table::from_csv(b"a\r\n1", limits),
        Err(Error::InputTooLarge)
    );

    let limits = Limits {
        max_columns: 1,
        ..Limits::default()
    };
    assert_eq!(
        Table::from_csv(b"a,b\r\n1,2", limits),
        Err(Error::TooManyColumns)
    );

    let limits = Limits {
        max_rows: 1,
        ..Limits::default()
    };
    assert_eq!(
        Table::from_csv(b"a\r\n1\r\n2", limits),
        Err(Error::TooManyRows)
    );

    let limits = Limits {
        max_cell_bytes: 1,
        ..Limits::default()
    };
    assert_eq!(
        Table::from_csv(b"aa\r\n1", limits),
        Err(Error::CellTooLarge)
    );

    let limits = Limits {
        max_cells: 1,
        ..Limits::default()
    };
    assert_eq!(
        Table::from_csv(b"a,b\r\n1,2", limits),
        Err(Error::TooManyCells)
    );

    let limits = Limits {
        max_total_lineage: 1,
        ..Limits::default()
    };
    assert_eq!(
        Table::from_csv(b"a,b\r\n1,2", limits),
        Err(Error::LineageTooLarge)
    );

    let limits = Limits {
        max_lineage_per_cell: 0,
        ..Limits::default()
    };
    assert_eq!(
        Table::from_csv(b"a\r\n1", limits),
        Err(Error::LineageTooLarge)
    );
}

#[test]
fn recipe_transform_and_export_resource_ceilings_fail_closed() {
    let limits = Limits {
        max_steps: 1,
        ..Limits::default()
    };
    assert_eq!(
        Recipe::from_json(
            br#"{"operations":[{"op":"limit","rows":1},{"op":"limit","rows":1}]}"#,
            limits
        ),
        Err(Error::TooManySteps)
    );

    let limits = Limits {
        max_output_bytes: 3,
        ..Limits::default()
    };
    let selected = recipe(r#"{"operations":[{"op":"select","columns":["a"]}]}"#);
    assert_eq!(
        reshape(table("a\r\n1"), &selected, limits),
        Err(Error::OutputTooLarge)
    );

    let limits = Limits {
        max_lineage_per_cell: 1,
        ..Limits::default()
    };
    let grouped = recipe(
        r#"{"operations":[{"op":"group","by":["a"],"aggregates":[{"column":"b","as":"total","function":"sum","mode":"decimal"}]}]}"#,
    );
    assert_eq!(
        reshape(table("a,b\r\nx,1\r\nx,2"), &grouped, limits),
        Err(Error::LineageTooLarge)
    );

    let limits = Limits {
        max_total_lineage: 1,
        ..Limits::default()
    };
    assert_eq!(
        reshape(
            table("a,b\r\nx,1"),
            &recipe(r#"{"operations":[{"op":"select","columns":["a","b"]}]}"#),
            limits
        ),
        Err(Error::LineageTooLarge)
    );

    let limits = Limits {
        max_total_lineage: 5,
        ..Limits::default()
    };
    let duplicated_lineage = recipe(
        r#"{"operations":[{"op":"group","by":["a"],"aggregates":[{"column":"b","as":"sum","function":"sum","mode":"decimal"},{"column":"b","as":"count","function":"count"}]}]}"#,
    );
    assert_eq!(
        reshape(table("a,b\r\nx,1\r\nx,2"), &duplicated_lineage, limits),
        Err(Error::LineageTooLarge)
    );

    let limits = Limits {
        max_cells: 7,
        ..Limits::default()
    };
    let wider_group = recipe(
        r#"{"operations":[{"op":"group","by":["a"],"aggregates":[{"column":"b","as":"count","function":"count"},{"column":"b","as":"minimum","function":"minimum"},{"column":"b","as":"maximum","function":"maximum"}]}]}"#,
    );
    assert_eq!(
        reshape(table("a,b\r\nx,1\r\ny,2"), &wider_group, limits),
        Err(Error::TooManyCells)
    );
}

#[test]
fn exact_decimal_order_and_overflow_are_closed_at_u128_scale_boundaries() {
    let ordered = csv(
        concat!(
            "id,value\r\n",
            "a,340282366920938463463374607431768211455\r\n",
            "b,34028236692093846346337460743176821145.5\r\n",
            "c,-340282366920938463463374607431768211455\r\n",
            "d,0.000000000000000001\r\n"
        ),
        r#"{"operations":[{"op":"order","by":[{"column":"value","direction":"ascending","mode":"decimal"}]}]}"#,
    );
    assert_eq!(
        ordered,
        Ok(concat!(
            "id,value\r\n",
            "c,-340282366920938463463374607431768211455\r\n",
            "d,0.000000000000000001\r\n",
            "b,34028236692093846346337460743176821145.5\r\n",
            "a,340282366920938463463374607431768211455\r\n"
        )
        .to_owned())
    );

    let sum = reshape(
        table("group,value\r\nx,340282366920938463463374607431768211455\r\nx,1\r\n"),
        &recipe(
            r#"{"operations":[{"op":"group","by":["group"],"aggregates":[{"column":"value","as":"sum","function":"sum","mode":"decimal"}]}]}"#,
        ),
        Limits::default(),
    );
    assert_eq!(sum, Err(Error::DecimalOverflow));

    let too_precise = csv(
        "value\r\n0.0000000000000000001\r\n",
        r#"{"operations":[{"op":"order","by":[{"column":"value","direction":"ascending","mode":"decimal"}]}]}"#,
    );
    assert_eq!(too_precise, Err(Error::DecimalOverflow));
}

struct CancelAfter {
    polls: Cell<u32>,
    allowance: u32,
}

impl Cancellation for CancelAfter {
    fn is_cancelled(&self) -> bool {
        let polls = self.polls.get();
        self.polls.set(polls.saturating_add(1));
        polls >= self.allowance
    }
}

#[test]
fn cancellation_is_polled_during_row_work_and_returns_no_partial_table() {
    let cancellation = CancelAfter {
        polls: Cell::new(0),
        allowance: 2,
    };
    let result = reshape_with_cancellation(
        table("a\r\n3\r\n2\r\n1\r\n0"),
        &recipe(
            r#"{"operations":[{"op":"order","by":[{"column":"a","direction":"ascending","mode":"decimal"}]}]}"#,
        ),
        Limits::default(),
        &cancellation,
    );
    assert_eq!(result, Err(Error::Cancelled));
    assert!(cancellation.polls.get() >= 3);
}
