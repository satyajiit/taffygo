// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use table_engine::{CsvErrorKind, Error, Limits, Recipe, Table};

use super::csv;

#[test]
fn malformed_csv_is_refused_without_repair() {
    let fixtures = [
        ("a,b\n1,2\n", CsvErrorKind::LoneLineBreak),
        ("a,b\r1,2", CsvErrorKind::LoneLineBreak),
        ("a,b\r\n1,2\"", CsvErrorKind::QuoteInUnquotedField),
        ("a,b\r\n\"x\"z,2", CsvErrorKind::BytesAfterClosingQuote),
        ("a,b\r\n\"x,2", CsvErrorKind::UnterminatedQuotedField),
        ("a,a\r\n1,2", CsvErrorKind::DuplicateHeader),
        ("a,b\r\n1", CsvErrorKind::RaggedRow),
        ("a,\r\n1,2", CsvErrorKind::EmptyHeader),
        ("a,b\r\n1,\t2", CsvErrorKind::DisallowedControl),
    ];
    for (input, reason) in fixtures {
        assert_eq!(
            Table::from_csv(input.as_bytes(), Limits::default()),
            Err(Error::InvalidCsv(reason)),
            "{input:?}"
        );
    }
    assert_eq!(
        Table::from_csv(b"a\0,b\r\n1,2", Limits::default()),
        Err(Error::InvalidCsv(CsvErrorKind::NulByte))
    );
    assert_eq!(
        Table::from_csv(&[b'a', b'\r', b'\n', 0xff], Limits::default()),
        Err(Error::InvalidUtf8)
    );
}

#[test]
fn export_neutralizes_formula_shaped_text_without_corrupting_numbers() {
    let output = csv(
        "name,value\r\na,=2+3\r\nb,+SUM(A1:A2)\r\nc,-1+2\r\nd,@cmd\r\ne,-10.50\r\nf,+10.50\r\ng,\"  =hidden\"\r\nh,'safe\r\n",
        r#"{"operations":[{"op":"select","columns":["name","value"]}]}"#,
    );
    assert_eq!(
        output,
        Ok(concat!(
            "name,value\r\n",
            "a,'=2+3\r\n",
            "b,'+SUM(A1:A2)\r\n",
            "c,'-1+2\r\n",
            "d,'@cmd\r\n",
            "e,-10.50\r\n",
            "f,+10.50\r\n",
            "g,'  =hidden\r\n",
            "h,'safe\r\n"
        )
        .to_owned())
    );
}

#[test]
fn export_hardens_formula_prefixes_after_invisible_padding() {
    let output = csv(
        concat!(
            "id,value\r\n",
            "a,\"\u{2003}@cmd\"\r\n",
            "b,\"\r\n-SUM(A1:A2)\"\r\n",
            "c,\"\u{feff}=hidden\"\r\n",
            "d,\"\u{200b}+hidden\"\r\n"
        ),
        r#"{"operations":[{"op":"select","columns":["id","value"]}]}"#,
    );
    assert_eq!(
        output,
        Ok(concat!(
            "id,value\r\n",
            "a,'\u{2003}@cmd\r\n",
            "b,\"'\r\n-SUM(A1:A2)\"\r\n",
            "c,'\u{feff}=hidden\r\n",
            "d,'\u{200b}+hidden\r\n"
        )
        .to_owned())
    );
}

#[test]
fn recipes_are_closed_strict_typed_and_bounded() {
    for invalid in [
        r#"{"operations":[]}"#,
        r#"{"operations":[{"op":"select","columns":["a"],"extra":true}]}"#,
        r#"{"operations":[{"op":"select","columns":["a"],"columns":["b"]}]}"#,
        r#"{"operations":[{"op":"unknown","columns":["a"]}]}"#,
        r#"{"operations":[{"op":"filter","column":"a","predicate":"equal"}]}"#,
        r#"{"operations":[{"op":"filter","column":"a","predicate":"is_empty","value":"x"}]}"#,
        r#"{"operations":[{"op":"filter","column":"a","predicate":"contains","value":"1","mode":"decimal"}]}"#,
        r#"{"operations":[{"op":"order","by":[{"column":"a","direction":"ascending"},{"column":"a","direction":"descending"}]}]}"#,
        r#"{"operations":[{"op":"group","by":["a"],"aggregates":[{"column":"b","as":"sum","function":"sum"}]}]}"#,
        r#"{"operations":[{"op":"pivot","rows":["a"],"column":"b","value":"c","aggregate":"sum"}]}"#,
        r#"{"operations":[{"op":"select","columns":["a"]}]} trailing"#,
    ] {
        assert!(
            Recipe::from_json(invalid.as_bytes(), Limits::default()).is_err(),
            "{invalid}"
        );
    }
    let limits = Limits {
        max_recipe_bytes: 8,
        ..Limits::default()
    };
    assert_eq!(
        Recipe::from_json(br#"{"operations":[]}"#, limits),
        Err(Error::RecipeTooLarge)
    );
}
