// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Provider replies accept whole-number floats; the catalog reader does not.
//!
//! A leftover fraction is kept as a decimal spelling and never rounded into
//! an integer. Catalog documents still fail closed on the same token.

#![allow(clippy::unwrap_used, clippy::expect_used)]

use model_router::json::{parse, parse_provider, JsonErrorKind, JsonValue};

#[test]
fn the_catalog_reader_still_refuses_a_fractional_token() {
    let error = parse("1.0").expect_err("catalog documents have no fractional field");
    assert_eq!(error.kind, JsonErrorKind::NonIntegerNumber);
    assert!(parse("1e3").is_err());
}

#[test]
fn a_whole_number_float_is_the_integer_it_names() {
    assert_eq!(parse_provider("1.0").unwrap(), JsonValue::Integer(1));
    assert_eq!(parse_provider("2.00").unwrap(), JsonValue::Integer(2));
    assert_eq!(parse_provider("-3.0").unwrap(), JsonValue::Integer(-3));
    assert_eq!(parse_provider("0.0").unwrap(), JsonValue::Integer(0));
    assert_eq!(parse_provider("1e3").unwrap(), JsonValue::Integer(1000));
    assert_eq!(parse_provider("1.50e1").unwrap(), JsonValue::Integer(15));
}

#[test]
fn a_leftover_fraction_is_kept_and_never_rounded() {
    let value = parse_provider("1.5").unwrap();
    assert_eq!(value, JsonValue::Decimal("1.5".to_owned()));
    assert_eq!(value.as_i64(), None);
    let tenth = parse_provider("1.0e-1").unwrap();
    assert_eq!(tenth.as_i64(), None);
}

#[test]
fn exact_integer_recognition_preserves_limits_and_reconstruction_ceiling() {
    assert_eq!(
        parse_provider("9223372036854775807.0").unwrap(),
        JsonValue::Integer(i64::MAX)
    );
    assert_eq!(
        parse_provider("-9223372036854775808.0").unwrap(),
        JsonValue::Integer(i64::MIN)
    );
    assert_eq!(
        parse_provider("9223372036854775808.0").unwrap(),
        JsonValue::Decimal("9223372036854775808.0".to_owned())
    );
    assert_eq!(parse_provider("0e18").unwrap(), JsonValue::Integer(0));
    assert_eq!(
        parse_provider("0e100").unwrap(),
        JsonValue::Decimal("0e100".to_owned())
    );
}

#[test]
fn a_reply_body_with_float_usage_still_reads() {
    let body = r#"{"stop_reason":"end_turn","content":[],"usage":{"input_tokens":1.0,"output_tokens":2.0}}"#;
    let document = parse_provider(body).unwrap();
    let usage = document.field("usage").unwrap();
    assert_eq!(
        usage.field("input_tokens").and_then(JsonValue::as_i64),
        Some(1)
    );
    assert_eq!(
        usage.field("output_tokens").and_then(JsonValue::as_i64),
        Some(2)
    );
}
