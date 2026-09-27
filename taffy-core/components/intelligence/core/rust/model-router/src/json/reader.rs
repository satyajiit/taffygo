// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded recursive-descent mechanics for the public JSON facade.

use std::collections::BTreeMap;

use super::number::exact_i64;
use super::{JsonError, JsonErrorKind, JsonValue, NumberSyntax, MAX_DEPTH, MAX_INPUT_BYTES};

pub(super) fn parse_with(input: &str, numbers: NumberSyntax) -> Result<JsonValue, JsonError> {
    if input.len() > MAX_INPUT_BYTES {
        return Err(JsonError {
            kind: JsonErrorKind::InputTooLarge,
            offset: 0,
        });
    }
    let mut reader = Reader {
        bytes: input.as_bytes(),
        pos: 0,
        numbers,
    };
    let value = reader.value(0)?;
    reader.skip_whitespace();
    if reader.peek().is_some() {
        return Err(reader.error(JsonErrorKind::TrailingBytes));
    }
    Ok(value)
}

struct Reader<'a> {
    bytes: &'a [u8],
    pos: usize,
    numbers: NumberSyntax,
}

impl Reader<'_> {
    fn error(&self, kind: JsonErrorKind) -> JsonError {
        JsonError {
            kind,
            offset: self.pos,
        }
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.pos).copied()
    }

    fn bump(&mut self) -> Option<u8> {
        let byte = self.peek()?;
        self.pos += 1;
        Some(byte)
    }

    fn skip_whitespace(&mut self) {
        while let Some(byte) = self.peek() {
            if matches!(byte, b' ' | b'\t' | b'\n' | b'\r') {
                self.pos += 1;
            } else {
                break;
            }
        }
    }

    fn expect(&mut self, byte: u8) -> Result<(), JsonError> {
        if self.peek() == Some(byte) {
            self.pos += 1;
            Ok(())
        } else {
            Err(self.error(JsonErrorKind::UnexpectedByte))
        }
    }

    fn literal(&mut self, word: &[u8]) -> Result<(), JsonError> {
        for expected in word {
            match self.bump() {
                Some(byte) if byte == *expected => {}
                Some(_) => return Err(self.error(JsonErrorKind::UnexpectedByte)),
                None => return Err(self.error(JsonErrorKind::UnexpectedEnd)),
            }
        }
        Ok(())
    }

    fn value(&mut self, depth: usize) -> Result<JsonValue, JsonError> {
        if depth > MAX_DEPTH {
            return Err(self.error(JsonErrorKind::TooDeep));
        }
        self.skip_whitespace();
        match self.peek() {
            None => Err(self.error(JsonErrorKind::UnexpectedEnd)),
            Some(b'n') => {
                self.literal(b"null")?;
                Ok(JsonValue::Null)
            }
            Some(b't') => {
                self.literal(b"true")?;
                Ok(JsonValue::Bool(true))
            }
            Some(b'f') => {
                self.literal(b"false")?;
                Ok(JsonValue::Bool(false))
            }
            Some(b'"') => self.string().map(JsonValue::Text),
            Some(b'[') => self.array(depth),
            Some(b'{') => self.object(depth),
            Some(byte) if byte == b'-' || byte.is_ascii_digit() => self.number(),
            Some(_) => Err(self.error(JsonErrorKind::UnexpectedByte)),
        }
    }

    fn array(&mut self, depth: usize) -> Result<JsonValue, JsonError> {
        self.expect(b'[')?;
        let mut items = Vec::new();
        self.skip_whitespace();
        if self.peek() == Some(b']') {
            self.pos += 1;
            return Ok(JsonValue::Array(items));
        }
        loop {
            items.push(self.value(depth + 1)?);
            self.skip_whitespace();
            match self.bump() {
                Some(b',') => {}
                Some(b']') => return Ok(JsonValue::Array(items)),
                Some(_) => {
                    self.pos -= 1;
                    return Err(self.error(JsonErrorKind::UnexpectedByte));
                }
                None => return Err(self.error(JsonErrorKind::UnexpectedEnd)),
            }
        }
    }

    fn object(&mut self, depth: usize) -> Result<JsonValue, JsonError> {
        self.expect(b'{')?;
        let mut map = BTreeMap::new();
        self.skip_whitespace();
        if self.peek() == Some(b'}') {
            self.pos += 1;
            return Ok(JsonValue::Object(map));
        }
        loop {
            self.skip_whitespace();
            let key = self.string()?;
            self.skip_whitespace();
            self.expect(b':')?;
            let value = self.value(depth + 1)?;
            if map.insert(key, value).is_some() {
                return Err(self.error(JsonErrorKind::DuplicateKey));
            }
            self.skip_whitespace();
            match self.bump() {
                Some(b',') => {}
                Some(b'}') => return Ok(JsonValue::Object(map)),
                Some(_) => {
                    self.pos -= 1;
                    return Err(self.error(JsonErrorKind::UnexpectedByte));
                }
                None => return Err(self.error(JsonErrorKind::UnexpectedEnd)),
            }
        }
    }

    fn number(&mut self) -> Result<JsonValue, JsonError> {
        let start = self.pos;
        if self.peek() == Some(b'-') {
            self.pos += 1;
        }
        let digits_start = self.pos;
        while self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
            self.pos += 1;
        }
        if self.pos == digits_start {
            return Err(self.error(JsonErrorKind::MalformedNumber));
        }
        let leading = self.bytes.get(digits_start).copied();
        if leading == Some(b'0') && self.pos - digits_start > 1 {
            return Err(JsonError {
                kind: JsonErrorKind::MalformedNumber,
                offset: digits_start,
            });
        }
        let mut extended = false;
        if matches!(self.numbers, NumberSyntax::JsonNumber) {
            if self.peek() == Some(b'.') {
                self.pos += 1;
                let fraction_start = self.pos;
                while self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
                    self.pos += 1;
                }
                if self.pos == fraction_start {
                    return Err(self.error(JsonErrorKind::MalformedNumber));
                }
                extended = true;
            }
            if self.peek().is_some_and(|byte| matches!(byte, b'e' | b'E')) {
                self.pos += 1;
                if self.peek().is_some_and(|byte| matches!(byte, b'+' | b'-')) {
                    self.pos += 1;
                }
                let exponent_start = self.pos;
                while self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
                    self.pos += 1;
                }
                if self.pos == exponent_start {
                    return Err(self.error(JsonErrorKind::MalformedNumber));
                }
                extended = true;
            }
        } else if self
            .peek()
            .is_some_and(|byte| matches!(byte, b'.' | b'e' | b'E'))
        {
            return Err(self.error(JsonErrorKind::NonIntegerNumber));
        }
        let text = self
            .bytes
            .get(start..self.pos)
            .and_then(|slice| core::str::from_utf8(slice).ok())
            .ok_or_else(|| self.error(JsonErrorKind::MalformedNumber))?;
        if !extended {
            return text
                .parse::<i64>()
                .map(JsonValue::Integer)
                .map_err(|_| JsonError {
                    kind: JsonErrorKind::NonIntegerNumber,
                    offset: start,
                });
        }
        Ok(exact_i64(text).map_or_else(|| JsonValue::Decimal(text.to_owned()), JsonValue::Integer))
    }

    /// Reads one string, copying an unescaped run once rather than growing an
    /// output buffer one scalar at a time. Escaped strings allocate only when
    /// the first escape makes reconstruction necessary.
    fn string(&mut self) -> Result<String, JsonError> {
        self.expect(b'"')?;
        let string_start = self.pos;
        let mut chunk_start = self.pos;
        let mut out: Option<String> = None;
        loop {
            let byte = self.peek().ok_or(JsonError {
                kind: JsonErrorKind::UnexpectedEnd,
                offset: self.pos,
            })?;
            match byte {
                b'"' => {
                    let chunk = core::str::from_utf8(
                        self.bytes
                            .get(chunk_start..self.pos)
                            .ok_or_else(|| self.error(JsonErrorKind::MalformedString))?,
                    )
                    .map_err(|_| self.error(JsonErrorKind::MalformedString))?;
                    self.pos += 1;
                    if let Some(mut value) = out {
                        value.push_str(chunk);
                        return Ok(value);
                    }
                    return Ok(chunk.to_owned());
                }
                b'\\' => {
                    let chunk = core::str::from_utf8(
                        self.bytes
                            .get(chunk_start..self.pos)
                            .ok_or_else(|| self.error(JsonErrorKind::MalformedString))?,
                    )
                    .map_err(|_| self.error(JsonErrorKind::MalformedString))?;
                    let initial_capacity = self
                        .pos
                        .saturating_sub(string_start)
                        .saturating_mul(2)
                        .max(16);
                    let value = out.get_or_insert_with(|| String::with_capacity(initial_capacity));
                    value.push_str(chunk);
                    self.pos += 1;
                    self.escape(value)?;
                    chunk_start = self.pos;
                }
                0x00..=0x1f => return Err(self.error(JsonErrorKind::MalformedString)),
                _ => {
                    let start = self.pos;
                    self.pos += 1;
                    self.pos = self
                        .pos
                        .checked_add(utf8_continuation_len(byte))
                        .ok_or_else(|| self.error(JsonErrorKind::MalformedString))?;
                    let encoded = self
                        .bytes
                        .get(start..self.pos)
                        .ok_or_else(|| self.error(JsonErrorKind::MalformedString))?;
                    core::str::from_utf8(encoded)
                        .map_err(|_| self.error(JsonErrorKind::MalformedString))?;
                }
            }
        }
    }

    fn escape(&mut self, out: &mut String) -> Result<(), JsonError> {
        let byte = self.bump().ok_or(JsonError {
            kind: JsonErrorKind::UnexpectedEnd,
            offset: self.pos,
        })?;
        let resolved = match byte {
            b'"' => '"',
            b'\\' => '\\',
            b'/' => '/',
            b'b' => '\u{0008}',
            b'f' => '\u{000c}',
            b'n' => '\n',
            b'r' => '\r',
            b't' => '\t',
            b'u' => return self.unicode_escape(out),
            _ => {
                self.pos -= 1;
                return Err(self.error(JsonErrorKind::MalformedString));
            }
        };
        out.push(resolved);
        Ok(())
    }

    fn unicode_escape(&mut self, out: &mut String) -> Result<(), JsonError> {
        let first = self.hex4()?;
        let scalar = if (0xd800..0xdc00).contains(&first) {
            self.expect(b'\\')
                .map_err(|_| self.error(JsonErrorKind::MalformedString))?;
            self.expect(b'u')
                .map_err(|_| self.error(JsonErrorKind::MalformedString))?;
            let second = self.hex4()?;
            if !(0xdc00..0xe000).contains(&second) {
                return Err(self.error(JsonErrorKind::MalformedString));
            }
            0x1_0000 + ((first - 0xd800) << 10) + (second - 0xdc00)
        } else if (0xdc00..0xe000).contains(&first) {
            return Err(self.error(JsonErrorKind::MalformedString));
        } else {
            first
        };
        let resolved =
            char::from_u32(scalar).ok_or_else(|| self.error(JsonErrorKind::MalformedString))?;
        out.push(resolved);
        Ok(())
    }

    fn hex4(&mut self) -> Result<u32, JsonError> {
        let mut value = 0u32;
        for _ in 0..4 {
            let byte = self.bump().ok_or(JsonError {
                kind: JsonErrorKind::UnexpectedEnd,
                offset: self.pos,
            })?;
            let digit = char::from(byte)
                .to_digit(16)
                .ok_or_else(|| self.error(JsonErrorKind::MalformedString))?;
            value = value * 16 + digit;
        }
        Ok(value)
    }
}

const fn utf8_continuation_len(lead: u8) -> usize {
    match lead {
        0xc0..=0xdf => 1,
        0xe0..=0xef => 2,
        0xf0..=0xf7 => 3,
        _ => 0,
    }
}
