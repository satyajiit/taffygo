// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The deliberately small JSON shape accepted by native table recipes.
//!
//! This reader exists because a shipping serialization-format dependency is
//! larger than the closed recipe it would parse. It is depth-bounded,
//! duplicate-key-rejecting, strict UTF-8, and total over arbitrary input.

use std::collections::BTreeMap;

use crate::Error;

const MAX_DEPTH: usize = 32;

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum Value {
    Object(BTreeMap<String, Value>),
    Array(Vec<Value>),
    Text(String),
    Unsigned(u64),
    Other,
}

pub(crate) fn parse(input: &[u8]) -> Result<Value, Error> {
    let text = core::str::from_utf8(input).map_err(|_| Error::InvalidUtf8)?;
    Parser::new(text).parse()
}

struct Parser<'a> {
    input: &'a str,
    cursor: usize,
}

impl<'a> Parser<'a> {
    const fn new(input: &'a str) -> Self {
        Self { input, cursor: 0 }
    }

    fn parse(mut self) -> Result<Value, Error> {
        self.skip_whitespace();
        let value = self.value(0)?;
        self.skip_whitespace();
        if self.cursor == self.input.len() {
            Ok(value)
        } else {
            Err(Error::InvalidRecipe)
        }
    }

    fn value(&mut self, depth: usize) -> Result<Value, Error> {
        if depth > MAX_DEPTH {
            return Err(Error::InvalidRecipe);
        }
        match self.peek() {
            Some(b'{') => self.object(depth),
            Some(b'[') => self.array(depth),
            Some(b'"') => self.string().map(Value::Text),
            Some(b't') => self.literal(b"true").map(|()| Value::Other),
            Some(b'f') => self.literal(b"false").map(|()| Value::Other),
            Some(b'n') => self.literal(b"null").map(|()| Value::Other),
            Some(b'0'..=b'9') => self.unsigned().map(Value::Unsigned),
            Some(_) | None => Err(Error::InvalidRecipe),
        }
    }

    fn object(&mut self, depth: usize) -> Result<Value, Error> {
        self.advance();
        self.skip_whitespace();
        let mut object = BTreeMap::new();
        if self.take(b'}') {
            return Ok(Value::Object(object));
        }
        loop {
            if self.peek() != Some(b'"') {
                return Err(Error::InvalidRecipe);
            }
            let key = self.string()?;
            self.skip_whitespace();
            if !self.take(b':') {
                return Err(Error::InvalidRecipe);
            }
            self.skip_whitespace();
            let value = self.value(depth.saturating_add(1))?;
            if object.insert(key, value).is_some() {
                return Err(Error::InvalidRecipe);
            }
            self.skip_whitespace();
            if self.take(b'}') {
                return Ok(Value::Object(object));
            }
            if !self.take(b',') {
                return Err(Error::InvalidRecipe);
            }
            self.skip_whitespace();
        }
    }

    fn array(&mut self, depth: usize) -> Result<Value, Error> {
        self.advance();
        self.skip_whitespace();
        let mut array = Vec::new();
        if self.take(b']') {
            return Ok(Value::Array(array));
        }
        loop {
            array.push(self.value(depth.saturating_add(1))?);
            self.skip_whitespace();
            if self.take(b']') {
                return Ok(Value::Array(array));
            }
            if !self.take(b',') {
                return Err(Error::InvalidRecipe);
            }
            self.skip_whitespace();
        }
    }

    fn string(&mut self) -> Result<String, Error> {
        if !self.take(b'"') {
            return Err(Error::InvalidRecipe);
        }
        let mut output = String::new();
        let mut segment = self.cursor;
        loop {
            let Some(byte) = self.peek() else {
                return Err(Error::InvalidRecipe);
            };
            match byte {
                b'"' => {
                    self.push_segment(&mut output, segment, self.cursor)?;
                    self.advance();
                    return Ok(output);
                }
                b'\\' => {
                    self.push_segment(&mut output, segment, self.cursor)?;
                    self.advance();
                    self.escape(&mut output)?;
                    segment = self.cursor;
                }
                0..=31 => return Err(Error::InvalidRecipe),
                _ => self.advance(),
            }
        }
    }

    fn push_segment(&self, output: &mut String, start: usize, end: usize) -> Result<(), Error> {
        let segment = self.input.get(start..end).ok_or(Error::InvalidRecipe)?;
        output.push_str(segment);
        Ok(())
    }

    fn escape(&mut self, output: &mut String) -> Result<(), Error> {
        let escaped = self.peek().ok_or(Error::InvalidRecipe)?;
        self.advance();
        match escaped {
            b'"' => output.push('"'),
            b'\\' => output.push('\\'),
            b'/' => output.push('/'),
            b'b' => output.push('\u{0008}'),
            b'f' => output.push('\u{000c}'),
            b'n' => output.push('\n'),
            b'r' => output.push('\r'),
            b't' => output.push('\t'),
            b'u' => self.unicode_escape(output)?,
            _ => return Err(Error::InvalidRecipe),
        }
        Ok(())
    }

    fn unicode_escape(&mut self, output: &mut String) -> Result<(), Error> {
        let high = self.hex_quad()?;
        let scalar = if (0xd800..=0xdbff).contains(&high) {
            if !self.take(b'\\') || !self.take(b'u') {
                return Err(Error::InvalidRecipe);
            }
            let low = self.hex_quad()?;
            if !(0xdc00..=0xdfff).contains(&low) {
                return Err(Error::InvalidRecipe);
            }
            0x1_0000 + ((u32::from(high) - 0xd800) << 10) + (u32::from(low) - 0xdc00)
        } else if (0xdc00..=0xdfff).contains(&high) {
            return Err(Error::InvalidRecipe);
        } else {
            u32::from(high)
        };
        output.push(char::from_u32(scalar).ok_or(Error::InvalidRecipe)?);
        Ok(())
    }

    fn hex_quad(&mut self) -> Result<u16, Error> {
        let mut value = 0_u16;
        for _ in 0..4 {
            let digit = match self.peek() {
                Some(b'0'..=b'9') => u16::from(self.peek().ok_or(Error::InvalidRecipe)? - b'0'),
                Some(b'a'..=b'f') => {
                    u16::from(self.peek().ok_or(Error::InvalidRecipe)? - b'a') + 10
                }
                Some(b'A'..=b'F') => {
                    u16::from(self.peek().ok_or(Error::InvalidRecipe)? - b'A') + 10
                }
                Some(_) | None => return Err(Error::InvalidRecipe),
            };
            self.advance();
            value = value
                .checked_mul(16)
                .and_then(|current| current.checked_add(digit))
                .ok_or(Error::InvalidRecipe)?;
        }
        Ok(value)
    }

    fn unsigned(&mut self) -> Result<u64, Error> {
        let first = self.peek().ok_or(Error::InvalidRecipe)?;
        if first == b'0' {
            self.advance();
            if self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
                return Err(Error::InvalidRecipe);
            }
            return self.number_terminator(0);
        }
        let mut value = 0_u64;
        while let Some(byte @ b'0'..=b'9') = self.peek() {
            self.advance();
            value = value
                .checked_mul(10)
                .and_then(|current| current.checked_add(u64::from(byte - b'0')))
                .ok_or(Error::InvalidRecipe)?;
        }
        self.number_terminator(value)
    }

    fn number_terminator(&self, value: u64) -> Result<u64, Error> {
        match self.peek() {
            None | Some(b' ' | b'\t' | b'\r' | b'\n' | b',' | b']' | b'}') => Ok(value),
            Some(_) => Err(Error::InvalidRecipe),
        }
    }

    fn literal(&mut self, expected: &[u8]) -> Result<(), Error> {
        let end = self
            .cursor
            .checked_add(expected.len())
            .ok_or(Error::InvalidRecipe)?;
        if self.input.as_bytes().get(self.cursor..end) != Some(expected) {
            return Err(Error::InvalidRecipe);
        }
        self.cursor = end;
        match self.peek() {
            None | Some(b' ' | b'\t' | b'\r' | b'\n' | b',' | b']' | b'}') => Ok(()),
            Some(_) => Err(Error::InvalidRecipe),
        }
    }

    fn skip_whitespace(&mut self) {
        while self
            .peek()
            .is_some_and(|byte| matches!(byte, b' ' | b'\t' | b'\r' | b'\n'))
        {
            self.advance();
        }
    }

    fn take(&mut self, expected: u8) -> bool {
        if self.peek() == Some(expected) {
            self.advance();
            true
        } else {
            false
        }
    }

    fn peek(&self) -> Option<u8> {
        self.input.as_bytes().get(self.cursor).copied()
    }

    fn advance(&mut self) {
        self.cursor = self.cursor.saturating_add(1);
    }
}

#[cfg(test)]
mod tests {
    use super::{parse, Value};
    use crate::Error;

    #[test]
    fn strings_decode_unicode_and_surrogate_pairs() {
        let parsed = parse(br#"{"plain":"Delhi","escaped":"\u0939\ud83e\uddc7"}"#);
        let Ok(Value::Object(object)) = parsed else {
            unreachable!("the object parses")
        };
        assert_eq!(object.get("escaped"), Some(&Value::Text("ह🧇".to_owned())));
    }

    #[test]
    fn duplicate_depth_number_and_string_ambiguity_are_refused() {
        for input in [
            br#"{"x":1,"x":2}"#.as_slice(),
            br#"{"x":01}"#.as_slice(),
            br#"{"x":1.0}"#.as_slice(),
            br#"{"x":"\ud800"}"#.as_slice(),
            br#"{"x":"\q"}"#.as_slice(),
            br#"{"x":true} trailing"#.as_slice(),
        ] {
            assert_eq!(parse(input), Err(Error::InvalidRecipe));
        }
        let deep = format!("{}0{}", "[".repeat(34), "]".repeat(34));
        assert_eq!(parse(deep.as_bytes()), Err(Error::InvalidRecipe));
    }
}
