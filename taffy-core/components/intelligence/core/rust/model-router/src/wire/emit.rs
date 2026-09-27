// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Appending JSON to a buffer the caller owns.
//!
//! The counterpart to [`crate::json`], and the reason it is a separate,
//! deliberately small thing rather than a serializer: **it never takes
//! ownership of anything it writes.** Every string arrives as `&str` and goes
//! straight into the caller's buffer, so no request body — and therefore no
//! page-derived material inside one — is ever held in a value this crate owns.
//! A serializer would need an intermediate document, and that document would
//! be exactly the thing this crate exists not to hold.
//!
//! [`Json::open_text`] is what makes that possible for the one family whose
//! turns carry a single string: several pieces are escaped straight into the
//! open literal, so joining them costs no allocation and no copy.
//!
//! It emits only what the four dialect tables need. There is no number type
//! but `u64`, no floating point, and no way to write a raw fragment — a raw
//! fragment is how caller-supplied bytes become structure, and nothing here
//! offers that door.
//!
//! [`Json::embedded_value`] is the one place that could have been such a door
//! and is not. Two families want a tool call's arguments as a JSON document
//! *inside a string*, and the obvious way to produce one is to accept the text
//! the reply delivered and quote it. That would splice caller bytes: a value
//! ending the string early puts whatever follows it into the request as
//! structure. So the argument arrives decoded and its safe spans are copied
//! directly between escapes, which cannot end the string and needs no
//! intermediate buffer to hold what it is escaping.

use std::collections::BTreeMap;

use crate::json::JsonValue;

/// A JSON writer over a borrowed buffer.
#[derive(Debug)]
pub(super) struct Json<'a> {
    out: &'a mut String,
    start: usize,
    limit: usize,
    attempted: usize,
    overflowed: bool,
    /// One flag per open container: whether it already holds something, which
    /// is what decides whether a comma comes first.
    open: Vec<bool>,
    /// Whether the next value fills a key that was just written, in which case
    /// it must not be preceded by a comma.
    pending_key: bool,
}

impl<'a> Json<'a> {
    /// Wraps a buffer and stops retaining this document past `limit` bytes.
    pub(super) fn with_limit(out: &'a mut String, limit: usize) -> Self {
        let start = out.len();
        Self {
            out,
            start,
            limit,
            attempted: 0,
            overflowed: false,
            open: Vec::new(),
            pending_key: false,
        }
    }

    /// Bytes the complete document would occupy, including discarded suffixes.
    pub(super) const fn attempted_bytes(&self) -> usize {
        self.attempted
    }

    fn append(&mut self, value: &str) {
        self.attempted = self.attempted.saturating_add(value.len());
        if self.attempted > self.limit {
            if !self.overflowed {
                self.out.truncate(self.start);
                self.overflowed = true;
            }
        } else if !self.overflowed {
            self.out.push_str(value);
        }
    }

    fn append_char(&mut self, value: char) {
        let mut encoded = [0_u8; 4];
        self.append(value.encode_utf8(&mut encoded));
    }

    fn separate(&mut self) {
        if self.pending_key {
            self.pending_key = false;
            return;
        }
        let needs_comma = self.open.last_mut().is_some_and(|filled| {
            let was_filled = *filled;
            *filled = true;
            was_filled
        });
        if needs_comma {
            self.append_char(',');
        }
    }

    /// Opens an object.
    pub(super) fn open_object(&mut self) {
        self.separate();
        self.append_char('{');
        self.open.push(false);
    }

    /// Closes the innermost object.
    pub(super) fn close_object(&mut self) {
        self.open.pop();
        self.append_char('}');
    }

    /// Opens an array.
    pub(super) fn open_array(&mut self) {
        self.separate();
        self.append_char('[');
        self.open.push(false);
    }

    /// Closes the innermost array.
    pub(super) fn close_array(&mut self) {
        self.open.pop();
        self.append_char(']');
    }

    /// Writes a key. The next value written fills it.
    pub(super) fn key(&mut self, name: &str) {
        self.separate();
        self.quoted(name);
        self.append_char(':');
        self.pending_key = true;
    }

    /// Writes a string.
    pub(super) fn text(&mut self, value: &str) {
        self.separate();
        self.quoted(value);
    }

    /// Writes a number.
    pub(super) fn number(&mut self, value: u64) {
        self.separate();
        self.append(&value.to_string());
    }

    /// Writes a boolean.
    pub(super) fn boolean(&mut self, value: bool) {
        self.separate();
        self.append(if value { "true" } else { "false" });
    }

    /// Writes JSON `null`.
    ///
    /// Present because on one family `null`, `false` and an absent key are
    /// three different answers to the same question, and only one of them is
    /// "the caller has no opinion".
    pub(super) fn null(&mut self) {
        self.separate();
        self.append("null");
    }

    /// Opens a string literal that further pieces are appended to.
    ///
    /// The pieces are escaped as they arrive, so a turn assembled from several
    /// borrowed fragments never exists as one owned string.
    pub(super) fn open_text(&mut self) {
        self.separate();
        self.append_char('"');
    }

    /// Appends one piece to the open string literal.
    pub(super) fn push_text(&mut self, piece: &str) {
        escape_into(self, piece);
    }

    /// Closes the open string literal.
    pub(super) fn close_text(&mut self) {
        self.append_char('"');
    }

    /// Writes an already-decoded value verbatim.
    ///
    /// Used for a tool's argument schema, which arrives decoded rather than as
    /// text precisely so that it cannot smuggle structure: a schema handed in
    /// as a string would have to be spliced in raw, and a raw splice of
    /// caller-supplied bytes is an injection with extra steps.
    pub(super) fn value(&mut self, value: &JsonValue) {
        match value {
            JsonValue::Null => {
                self.separate();
                self.append("null");
            }
            JsonValue::Bool(flag) => {
                self.separate();
                self.append(if *flag { "true" } else { "false" });
            }
            JsonValue::Integer(number) => {
                self.separate();
                self.append(&number.to_string());
            }
            JsonValue::Decimal(spelling) => {
                self.separate();
                self.append(spelling);
            }
            JsonValue::Text(text) => self.text(text),
            JsonValue::Array(items) => {
                self.open_array();
                for item in items {
                    self.value(item);
                }
                self.close_array();
            }
            JsonValue::Object(map) => self.object(map),
        }
    }

    /// Writes an already-decoded value as a JSON *document inside a string*.
    ///
    /// Two families deliver a tool call's arguments that way and want them
    /// back that way. The document is escaped directly into the literal rather
    /// than rendered and then quoted: rendering it first
    /// would need an owned buffer holding page-derived material, which is the
    /// thing this module exists not to have, and quoting text that arrived as
    /// text would splice caller bytes into the body — the door
    /// [`Json::value`] is careful not to open.
    pub(super) fn embedded_value(&mut self, value: &JsonValue) {
        self.separate();
        self.append_char('"');
        embed(self, value);
        self.append_char('"');
    }

    fn object(&mut self, map: &BTreeMap<String, JsonValue>) {
        self.open_object();
        for (name, item) in map {
            self.key(name);
            self.value(item);
        }
        self.close_object();
    }

    fn quoted(&mut self, value: &str) {
        self.append_char('"');
        escape_into(self, value);
        self.append_char('"');
    }
}

/// Escapes `value` into `out` without the quotes around it.
///
/// The input is a `&str`, so it is valid UTF-8 and cannot carry a lone
/// surrogate; multi-byte characters are copied through, which is legal JSON
/// and keeps a page's own script from being expanded sixfold in escapes.
fn escape_into(out: &mut Json<'_>, value: &str) {
    let mut run_start = 0;
    for (index, character) in value.char_indices() {
        let replacement = match character {
            '"' => Some("\\\""),
            '\\' => Some("\\\\"),
            '\n' => Some("\\n"),
            '\r' => Some("\\r"),
            '\t' => Some("\\t"),
            '\u{0008}' => Some("\\b"),
            '\u{000c}' => Some("\\f"),
            control if control < ' ' => None,
            _ => continue,
        };
        out.append(&value[run_start..index]);
        if let Some(replacement) = replacement {
            out.append(replacement);
        } else {
            append_control(out, character, "\\u");
        }
        run_start = index + character.len_utf8();
    }
    out.append(&value[run_start..]);
}

fn append_control(out: &mut Json<'_>, control: char, prefix: &str) {
    out.append(prefix);
    for shift in [12u32, 8, 4, 0] {
        let digit = (u32::from(control) >> shift) & 0xf;
        out.append_char(char::from_digit(digit, 16).unwrap_or('0'));
    }
}

/// Appends `value`'s JSON text to `out`, escaped one level further than it
/// already is, so that the result is the body of a string literal whose value
/// is that document.
///
/// Structural characters are copied through — a brace, a comma or a colon
/// needs no escape inside a string — and every character that would need one
/// gets the escape the inner document needs, escaped again for the outer
/// literal.
fn embed(out: &mut Json<'_>, value: &JsonValue) {
    match value {
        JsonValue::Null => out.append("null"),
        JsonValue::Bool(flag) => out.append(if *flag { "true" } else { "false" }),
        JsonValue::Integer(number) => out.append(&number.to_string()),
        JsonValue::Decimal(spelling) => out.append(spelling),
        JsonValue::Text(text) => embed_text(out, text),
        JsonValue::Array(items) => {
            out.append_char('[');
            for (index, item) in items.iter().enumerate() {
                if index > 0 {
                    out.append_char(',');
                }
                embed(out, item);
            }
            out.append_char(']');
        }
        JsonValue::Object(map) => {
            out.append_char('{');
            for (index, (name, item)) in map.iter().enumerate() {
                if index > 0 {
                    out.append_char(',');
                }
                embed_text(out, name);
                out.append_char(':');
                embed(out, item);
            }
            out.append_char('}');
        }
    }
}

/// Appends one string of the embedded document, its own quotes included.
///
/// The pairs are spelled out rather than composed from [`escape_into`] run
/// twice, because composing them would need the intermediate owned string this
/// module exists not to hold. Each arm is the escape the inner document needs
/// with every backslash in it doubled for the outer literal.
fn embed_text(out: &mut Json<'_>, value: &str) {
    // The quote that opens the embedded string, escaped for the outer literal.
    out.append("\\\"");
    let mut run_start = 0;
    for (index, character) in value.char_indices() {
        let replacement = match character {
            // Inner `\"`; the outer literal doubles its backslash.
            '"' => Some("\\\\\\\""),
            // Inner `\\`; both backslashes doubled.
            '\\' => Some("\\\\\\\\"),
            '\n' => Some("\\\\n"),
            '\r' => Some("\\\\r"),
            '\t' => Some("\\\\t"),
            '\u{0008}' => Some("\\\\b"),
            '\u{000c}' => Some("\\\\f"),
            control if control < ' ' => None,
            _ => continue,
        };
        out.append(&value[run_start..index]);
        if let Some(replacement) = replacement {
            out.append(replacement);
        } else {
            append_control(out, character, "\\\\u");
        }
        run_start = index + character.len_utf8();
    }
    out.append(&value[run_start..]);
    out.append("\\\"");
}
