// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A small, total JSON reader for catalog documents.
//!
//! The catalog is remote input to a trusted process, so it is parsed as
//! untrusted data: size-bounded, depth-bounded, duplicate-key-rejecting, and
//! free of any construct the catalog schema does not use. The reader is
//! deliberately strict — a document this parser refuses is a document the
//! model router never has to reason about.
//!
//! It exists because the shipping dependency tree stays near zero: every
//! third-party crate that ships must clear Chromium's vendoring review, so the
//! crate carries a reader instead of a general-purpose JSON library. The
//! catalog types still derive serde traits, and a test decodes the same
//! documents both ways and compares, so this reader cannot drift away from the
//! serde shape without failing.
//!
//! Catalog documents are integers only. The catalog schema has no fractional
//! field — prices are integer micro-units, limits are token counts — so a
//! fractional number is a defect rather than a value to round.
//!
//! Provider replies are a different document. Families emit `1.0` for a token
//! count that is the integer 1, and some bodies carry leftover fractions in
//! fields this crate never reads. [`parse_provider`] accepts a JSON number
//! token: a value that is exactly an `i64` becomes [`JsonValue::Integer`]; a
//! leftover fraction is [`JsonValue::Decimal`] and `as_i64` answers `None`.
//! Nothing is rounded. The catalog reader is unchanged.

use core::fmt;
use std::collections::BTreeMap;

mod number;
mod reader;

/// Deepest nesting the reader accepts.
pub const MAX_DEPTH: usize = 24;

/// Longest input the reader accepts, in bytes.
pub const MAX_INPUT_BYTES: usize = 4 * 1024 * 1024;

/// A decoded JSON value.
///
/// Objects keep sorted, unique keys: duplicate keys are refused rather than
/// resolved by position, because "last one wins" is how a catalog entry gets
/// two meanings.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum JsonValue {
    /// `null`.
    Null,
    /// `true` or `false`.
    Bool(bool),
    /// An integer.
    Integer(i64),
    /// A JSON number that is not an `i64` integer.
    ///
    /// The original spelling, so equality is structural and a writer can emit
    /// the same token. Catalog parsing never produces this member.
    Decimal(String),
    /// A string with escapes already resolved.
    Text(String),
    /// An array.
    Array(Vec<JsonValue>),
    /// An object.
    Object(BTreeMap<String, JsonValue>),
}

impl JsonValue {
    /// The object body, or `None` for any other shape.
    pub fn as_object(&self) -> Option<&BTreeMap<String, JsonValue>> {
        match self {
            Self::Object(map) => Some(map),
            _ => None,
        }
    }

    /// The array body, or `None` for any other shape.
    pub fn as_array(&self) -> Option<&[JsonValue]> {
        match self {
            Self::Array(items) => Some(items),
            _ => None,
        }
    }

    /// The string body, or `None` for any other shape.
    pub fn as_str(&self) -> Option<&str> {
        match self {
            Self::Text(text) => Some(text),
            _ => None,
        }
    }

    /// The integer body, or `None` for any other shape.
    pub fn as_i64(&self) -> Option<i64> {
        match self {
            Self::Integer(value) => Some(*value),
            _ => None,
        }
    }

    /// The boolean body, or `None` for any other shape.
    pub fn as_bool(&self) -> Option<bool> {
        match self {
            Self::Bool(value) => Some(*value),
            _ => None,
        }
    }

    /// Whether the value is `null`.
    pub fn is_null(&self) -> bool {
        matches!(self, Self::Null)
    }

    /// Looks a field up in an object value.
    pub fn field(&self, name: &str) -> Option<&JsonValue> {
        self.as_object().and_then(|map| map.get(name))
    }
}

/// Why a document was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum JsonErrorKind {
    /// The input exceeded [`MAX_INPUT_BYTES`].
    InputTooLarge,
    /// Nesting exceeded [`MAX_DEPTH`].
    TooDeep,
    /// The input ended in the middle of a value.
    UnexpectedEnd,
    /// A byte appeared where the grammar does not allow it.
    UnexpectedByte,
    /// Trailing bytes followed the top-level value.
    TrailingBytes,
    /// A number used fractional or exponent syntax, or did not fit in 64 bits.
    NonIntegerNumber,
    /// A number used a leading zero or a lone minus sign.
    MalformedNumber,
    /// A string contained a control byte, a bad escape, or invalid UTF-8.
    MalformedString,
    /// An object repeated a key.
    DuplicateKey,
}

impl fmt::Display for JsonErrorKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            Self::InputTooLarge => "input is larger than the accepted bound",
            Self::TooDeep => "nesting is deeper than the accepted bound",
            Self::UnexpectedEnd => "input ended inside a value",
            Self::UnexpectedByte => "unexpected byte",
            Self::TrailingBytes => "trailing bytes after the top-level value",
            Self::NonIntegerNumber => "number is fractional or out of range",
            Self::MalformedNumber => "malformed number",
            Self::MalformedString => "malformed string",
            Self::DuplicateKey => "duplicate object key",
        };
        f.write_str(text)
    }
}

/// A refusal with the byte offset it happened at.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct JsonError {
    /// What went wrong.
    pub kind: JsonErrorKind,
    /// Byte offset into the input.
    pub offset: usize,
}

impl fmt::Display for JsonError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} at byte {}", self.kind, self.offset)
    }
}

/// Reads one catalog JSON document. Numbers must be integers.
pub fn parse(input: &str) -> Result<JsonValue, JsonError> {
    parse_with(input, NumberSyntax::IntegerOnly)
}

/// Reads one provider reply. Whole-number floats become integers; leftover
/// fractions stay as [`JsonValue::Decimal`] and are never rounded.
pub fn parse_provider(input: &str) -> Result<JsonValue, JsonError> {
    parse_with(input, NumberSyntax::JsonNumber)
}

fn parse_with(input: &str, numbers: NumberSyntax) -> Result<JsonValue, JsonError> {
    reader::parse_with(input, numbers)
}

#[derive(Clone, Copy)]
pub(super) enum NumberSyntax {
    IntegerOnly,
    JsonNumber,
}
