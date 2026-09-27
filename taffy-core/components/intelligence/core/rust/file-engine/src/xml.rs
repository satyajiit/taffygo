// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::BTreeSet;

use crate::sink::TextSink;
use crate::{FileError, InputIssue, PackageIssue};

pub(crate) const DECLARATION: &str =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";

pub(crate) fn validate_text(value: &str) -> Result<(), FileError> {
    if value.chars().all(is_xml_character) {
        Ok(())
    } else {
        Err(FileError::InvalidInput(InputIssue::UnsupportedCharacter))
    }
}

pub(crate) fn escaped_text(sink: &mut TextSink, value: &str) -> Result<(), FileError> {
    validate_text(value)?;
    for character in value.chars() {
        match character {
            '&' => sink.push_str("&amp;")?,
            '<' => sink.push_str("&lt;")?,
            '>' => sink.push_str("&gt;")?,
            _ => sink.push_str(&character.to_string())?,
        }
    }
    Ok(())
}

pub(crate) fn escaped_attribute(sink: &mut TextSink, value: &str) -> Result<(), FileError> {
    validate_text(value)?;
    for character in value.chars() {
        match character {
            '&' => sink.push_str("&amp;")?,
            '<' => sink.push_str("&lt;")?,
            '>' => sink.push_str("&gt;")?,
            '"' => sink.push_str("&quot;")?,
            '\'' => sink.push_str("&apos;")?,
            _ => sink.push_str(&character.to_string())?,
        }
    }
    Ok(())
}

pub(crate) fn validate(bytes: &[u8]) -> Result<(), FileError> {
    let text = std::str::from_utf8(bytes)
        .map_err(|_| FileError::InvalidPackage(PackageIssue::InvalidXml))?;
    if !text.starts_with(DECLARATION) || text.contains("<!DOCTYPE") || text.contains("<!ENTITY") {
        return Err(FileError::InvalidPackage(PackageIssue::InvalidXml));
    }
    if !text.chars().all(is_xml_character) {
        return Err(FileError::InvalidPackage(PackageIssue::InvalidXml));
    }
    let body = text
        .strip_prefix(DECLARATION)
        .ok_or(FileError::InvalidPackage(PackageIssue::InvalidXml))?;
    let mut parser = Parser::new(body);
    parser.document()
}

fn is_xml_character(character: char) -> bool {
    matches!(character, '\u{9}' | '\u{A}' | '\u{D}')
        || ('\u{20}'..='\u{D7FF}').contains(&character)
        || ('\u{E000}'..='\u{FFFD}').contains(&character)
        || ('\u{10000}'..='\u{10FFFF}').contains(&character)
}

struct Parser<'a> {
    input: &'a [u8],
    cursor: usize,
    stack: Vec<&'a str>,
    saw_root: bool,
    closed_root: bool,
}

impl<'a> Parser<'a> {
    fn new(input: &'a str) -> Self {
        Self {
            input: input.as_bytes(),
            cursor: 0,
            stack: Vec::new(),
            saw_root: false,
            closed_root: false,
        }
    }

    fn document(&mut self) -> Result<(), FileError> {
        while self.cursor < self.input.len() {
            if self.peek() == Some(b'<') {
                self.tag()?;
            } else {
                self.text()?;
            }
        }
        if self.saw_root && self.closed_root && self.stack.is_empty() {
            Ok(())
        } else {
            Err(invalid_xml())
        }
    }

    fn tag(&mut self) -> Result<(), FileError> {
        self.take_byte(b'<')?;
        match self.peek() {
            Some(b'/') => self.end_tag(),
            Some(b'!' | b'?') | None => Err(invalid_xml()),
            Some(_) => self.start_tag(),
        }
    }

    fn start_tag(&mut self) -> Result<(), FileError> {
        if self.closed_root {
            return Err(invalid_xml());
        }
        let name = self.name()?;
        if self.stack.is_empty() {
            if self.saw_root {
                return Err(invalid_xml());
            }
            self.saw_root = true;
        }
        let mut attributes = BTreeSet::new();
        loop {
            self.space();
            match (self.peek(), self.peek_second()) {
                (Some(b'/'), Some(b'>')) => {
                    self.cursor = self.cursor.saturating_add(2);
                    if self.stack.is_empty() {
                        self.closed_root = true;
                    }
                    return Ok(());
                }
                (Some(b'>'), _) => {
                    self.cursor = self.cursor.saturating_add(1);
                    self.stack.push(name);
                    return Ok(());
                }
                (Some(_), _) => {
                    let attribute = self.attribute()?;
                    if !attributes.insert(attribute) {
                        return Err(invalid_xml());
                    }
                }
                (None, _) => return Err(invalid_xml()),
            }
        }
    }

    fn end_tag(&mut self) -> Result<(), FileError> {
        self.take_byte(b'/')?;
        let name = self.name()?;
        self.space();
        self.take_byte(b'>')?;
        let Some(open) = self.stack.pop() else {
            return Err(invalid_xml());
        };
        if open != name {
            return Err(invalid_xml());
        }
        if self.stack.is_empty() {
            self.closed_root = true;
        }
        Ok(())
    }

    fn attribute(&mut self) -> Result<&'a str, FileError> {
        let name = self.name()?;
        self.space();
        self.take_byte(b'=')?;
        self.space();
        let Some(quote @ (b'\'' | b'"')) = self.peek() else {
            return Err(invalid_xml());
        };
        self.cursor = self.cursor.saturating_add(1);
        while let Some(byte) = self.peek() {
            if byte == quote {
                self.cursor = self.cursor.saturating_add(1);
                return Ok(name);
            }
            if matches!(byte, b'<' | b'>' | b'\r' | b'\n') {
                return Err(invalid_xml());
            }
            if byte == b'&' {
                self.entity()?;
            } else {
                self.cursor = self.cursor.saturating_add(1);
            }
        }
        Err(invalid_xml())
    }

    fn text(&mut self) -> Result<(), FileError> {
        while let Some(byte) = self.peek() {
            if byte == b'<' {
                return Ok(());
            }
            if self.closed_root && !byte.is_ascii_whitespace() {
                return Err(invalid_xml());
            }
            if byte == b'&' {
                self.entity()?;
            } else {
                self.cursor = self.cursor.saturating_add(1);
            }
        }
        Ok(())
    }

    fn entity(&mut self) -> Result<(), FileError> {
        const ENTITIES: [&[u8]; 5] = [b"&amp;", b"&lt;", b"&gt;", b"&quot;", b"&apos;"];
        let remaining = self.input.get(self.cursor..).ok_or_else(invalid_xml)?;
        let Some(entity) = ENTITIES
            .into_iter()
            .find(|entity| remaining.starts_with(entity))
        else {
            return Err(invalid_xml());
        };
        self.cursor = self.cursor.saturating_add(entity.len());
        Ok(())
    }

    fn name(&mut self) -> Result<&'a str, FileError> {
        let start = self.cursor;
        let first = self.peek().ok_or_else(invalid_xml)?;
        if !first.is_ascii_alphabetic() && first != b'_' {
            return Err(invalid_xml());
        }
        self.cursor = self.cursor.saturating_add(1);
        while let Some(byte) = self.peek() {
            if byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b':' | b'-' | b'.') {
                self.cursor = self.cursor.saturating_add(1);
            } else {
                break;
            }
        }
        let bytes = self.input.get(start..self.cursor).ok_or_else(invalid_xml)?;
        std::str::from_utf8(bytes).map_err(|_| invalid_xml())
    }

    fn space(&mut self) {
        while self.peek().is_some_and(|byte| byte.is_ascii_whitespace()) {
            self.cursor = self.cursor.saturating_add(1);
        }
    }

    fn take_byte(&mut self, expected: u8) -> Result<(), FileError> {
        if self.peek() != Some(expected) {
            return Err(invalid_xml());
        }
        self.cursor = self.cursor.saturating_add(1);
        Ok(())
    }

    fn peek(&self) -> Option<u8> {
        self.input.get(self.cursor).copied()
    }

    fn peek_second(&self) -> Option<u8> {
        self.input.get(self.cursor.saturating_add(1)).copied()
    }
}

fn invalid_xml() -> FileError {
    FileError::InvalidPackage(PackageIssue::InvalidXml)
}

#[cfg(test)]
mod tests;
