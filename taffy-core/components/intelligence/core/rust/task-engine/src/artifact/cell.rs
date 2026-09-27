// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Neutralizing a value that came off a page.
//!
//! The security seam of this module, and the reason it is its own file. Every
//! value an artifact writes is treated as hostile input, because a value read
//! from a page is: control characters and bidirectional overrides are removed,
//! Markdown table and link syntax is escaped so nothing becomes a link, and a
//! comma-separated cell a spreadsheet would evaluate as a formula is prefixed
//! so it stays text.
//!
//! Nothing here knows what a fact, a source, or an artifact is. It takes a
//! string and returns a string that is safe in one destination, which is what
//! makes it testable against hostile input on its own.
/// Removes every control character and collapses runs of whitespace.
///
/// A tab, a newline, a carriage return, or a bidirectional override in a value
/// that came off a page would change how a row reads without changing what it
/// says, so none of them survives into an artifact.
pub(super) fn flatten(value: &str) -> String {
    let mut out = String::with_capacity(value.len());
    let mut pending_space = false;
    for character in value.chars() {
        if character.is_control() || is_bidirectional_control(character) {
            pending_space = true;
            continue;
        }
        if character.is_whitespace() {
            pending_space = true;
            continue;
        }
        if pending_space && !out.is_empty() {
            out.push(' ');
        }
        pending_space = false;
        out.push(character);
    }
    out
}

const fn is_bidirectional_control(character: char) -> bool {
    matches!(character, '\u{200e}' | '\u{200f}' | '\u{202a}'..='\u{202e}' | '\u{2066}'..='\u{2069}')
}

/// Escapes a value for a Markdown table cell.
///
/// Table delimiters, emphasis, code fences, and link syntax are all escaped, so
/// a value that came off a page renders as the text it is rather than as
/// markup. Nothing becomes a link.
pub(super) fn markdown_cell(value: &str) -> String {
    let flat = flatten(value);
    let mut out = String::with_capacity(flat.len().saturating_add(8));
    for character in flat.chars() {
        match character {
            '|' | '\\' | '`' | '*' | '_' | '[' | ']' | '(' | ')' | '<' | '>' | '#' | '&' => {
                out.push('\\');
                out.push(character);
            }
            _ => out.push(character),
        }
    }
    if out.is_empty() {
        out.push('—');
    }
    out
}

/// Escapes a value for a comma-separated cell.
///
/// Always quoted, with embedded quotes doubled. A value a spreadsheet would
/// evaluate — one starting with an equals sign, a plus, a minus, an at sign, or
/// a separator a locale might reinterpret — is prefixed with an apostrophe so
/// it stays text.
pub(super) fn csv_cell(value: &str) -> String {
    let flat = flatten(value);
    let neutralized = match flat.as_bytes().first() {
        Some(b'=' | b'+' | b'-' | b'@' | b'\t' | b'\r') => format!("'{flat}"),
        _ => flat,
    };
    let mut out = String::with_capacity(neutralized.len().saturating_add(2));
    out.push('"');
    for character in neutralized.chars() {
        if character == '"' {
            out.push('"');
        }
        out.push(character);
    }
    out.push('"');
    out
}

#[cfg(test)]
mod tests {
    use super::{csv_cell, markdown_cell};

    #[test]
    fn a_comma_separated_cell_that_a_spreadsheet_would_evaluate_stays_text() {
        for hostile in ["=1+1", "+SUM(A1)", "-2+3", "@SUM(A1)", "=cmd|'/c calc'!A0"] {
            let cell = csv_cell(hostile);
            assert!(cell.starts_with("\"'"), "{hostile} rendered as {cell}");
        }
        assert_eq!(csv_cell("plain"), "\"plain\"");
    }

    #[test]
    fn a_comma_separated_cell_doubles_its_quotes_and_loses_its_newlines() {
        assert_eq!(csv_cell("say \"hello\""), "\"say \"\"hello\"\"\"");
        assert_eq!(csv_cell("one\ntwo"), "\"one two\"");
        assert_eq!(csv_cell("one\r\ntwo"), "\"one two\"");
    }

    #[test]
    fn a_markdown_cell_never_becomes_markup_or_a_link() {
        assert_eq!(markdown_cell("a|b"), "a\\|b");
        assert_eq!(
            markdown_cell("[click](https://example.test)"),
            "\\[click\\]\\(https://example.test\\)"
        );
        assert_eq!(markdown_cell("**bold**"), "\\*\\*bold\\*\\*");
        assert_eq!(markdown_cell("=SUM(A1)"), "=SUM\\(A1\\)");
        assert_eq!(markdown_cell("<script>"), "\\<script\\>");
        assert_eq!(markdown_cell("`code`"), "\\`code\\`");
    }

    #[test]
    fn a_bidirectional_override_does_not_survive_into_a_cell() {
        assert_eq!(markdown_cell("safe\u{202e}txt.exe"), "safe txt.exe");
        assert_eq!(csv_cell("safe\u{202e}txt.exe"), "\"safe txt.exe\"");
    }
}
