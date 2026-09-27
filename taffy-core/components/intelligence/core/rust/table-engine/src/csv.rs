// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::decimal::Decimal;
use crate::{CellRef, CsvErrorKind, Error, Limits, SourcedCell, SourcedTable, Table};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum State {
    FieldStart,
    Unquoted,
    Quoted,
    AfterQuote,
}

pub(crate) fn parse(input: &[u8], limits: Limits) -> Result<Table, Error> {
    if input.is_empty() {
        return Err(Error::InvalidCsv(CsvErrorKind::Empty));
    }
    if input.len() > limits.max_input_bytes {
        return Err(Error::InputTooLarge);
    }
    Parser::new(limits).run(input)
}

struct Parser {
    parsed: Vec<Vec<String>>,
    row: Vec<String>,
    field: Vec<u8>,
    state: State,
    ended_with_record: bool,
    limits: Limits,
}

impl Parser {
    fn new(limits: Limits) -> Self {
        Self {
            parsed: Vec::new(),
            row: Vec::new(),
            field: Vec::new(),
            state: State::FieldStart,
            ended_with_record: false,
            limits,
        }
    }

    fn run(mut self, input: &[u8]) -> Result<Table, Error> {
        let mut index = 0_usize;
        while index < input.len() {
            let byte = *input
                .get(index)
                .ok_or(Error::InvalidCsv(CsvErrorKind::Empty))?;
            if byte == 0 {
                return Err(Error::InvalidCsv(CsvErrorKind::NulByte));
            }
            if is_disallowed_control(byte) {
                return Err(Error::InvalidCsv(CsvErrorKind::DisallowedControl));
            }
            self.ended_with_record = false;
            index = match self.state {
                State::FieldStart => self.at_field_start(input, index, byte)?,
                State::Unquoted => self.in_unquoted(input, index, byte)?,
                State::Quoted => self.in_quoted(input, index, byte)?,
                State::AfterQuote => self.after_quote(input, index, byte)?,
            };
        }
        if self.state == State::Quoted {
            return Err(Error::InvalidCsv(CsvErrorKind::UnterminatedQuotedField));
        }
        if !self.ended_with_record {
            self.finish_field()?;
            self.finish_row()?;
        }
        build_table(self.parsed, self.limits)
    }

    fn at_field_start(&mut self, input: &[u8], index: usize, byte: u8) -> Result<usize, Error> {
        match byte {
            b'"' => {
                self.state = State::Quoted;
                Ok(index.saturating_add(1))
            }
            b',' => {
                self.finish_field()?;
                Ok(index.saturating_add(1))
            }
            b'\r' => self.record_break(input, index),
            b'\n' => Err(Error::InvalidCsv(CsvErrorKind::LoneLineBreak)),
            _ => {
                self.push(byte)?;
                self.state = State::Unquoted;
                Ok(index.saturating_add(1))
            }
        }
    }

    fn in_unquoted(&mut self, input: &[u8], index: usize, byte: u8) -> Result<usize, Error> {
        match byte {
            b',' => {
                self.finish_field()?;
                self.state = State::FieldStart;
                Ok(index.saturating_add(1))
            }
            b'\r' => self.record_break(input, index),
            b'\n' => Err(Error::InvalidCsv(CsvErrorKind::LoneLineBreak)),
            b'"' => Err(Error::InvalidCsv(CsvErrorKind::QuoteInUnquotedField)),
            _ => {
                self.push(byte)?;
                Ok(index.saturating_add(1))
            }
        }
    }

    fn in_quoted(&mut self, input: &[u8], index: usize, byte: u8) -> Result<usize, Error> {
        match byte {
            b'"' if input.get(index.saturating_add(1)) == Some(&b'"') => {
                self.push(b'"')?;
                Ok(index.saturating_add(2))
            }
            b'"' => {
                self.state = State::AfterQuote;
                Ok(index.saturating_add(1))
            }
            b'\r' => {
                require_lf(input, index)?;
                self.push(b'\r')?;
                self.push(b'\n')?;
                Ok(index.saturating_add(2))
            }
            b'\n' => Err(Error::InvalidCsv(CsvErrorKind::LoneLineBreak)),
            _ => {
                self.push(byte)?;
                Ok(index.saturating_add(1))
            }
        }
    }

    fn after_quote(&mut self, input: &[u8], index: usize, byte: u8) -> Result<usize, Error> {
        match byte {
            b',' => {
                self.finish_field()?;
                self.state = State::FieldStart;
                Ok(index.saturating_add(1))
            }
            b'\r' => self.record_break(input, index),
            b'\n' => Err(Error::InvalidCsv(CsvErrorKind::LoneLineBreak)),
            _ => Err(Error::InvalidCsv(CsvErrorKind::BytesAfterClosingQuote)),
        }
    }

    fn record_break(&mut self, input: &[u8], index: usize) -> Result<usize, Error> {
        require_lf(input, index)?;
        self.finish_field()?;
        self.finish_row()?;
        self.state = State::FieldStart;
        self.ended_with_record = true;
        Ok(index.saturating_add(2))
    }

    fn push(&mut self, byte: u8) -> Result<(), Error> {
        push_field_byte(&mut self.field, byte, self.limits)
    }

    fn finish_field(&mut self) -> Result<(), Error> {
        finish_field(&mut self.row, &mut self.field)
    }

    fn finish_row(&mut self) -> Result<(), Error> {
        finish_row(&mut self.parsed, &mut self.row, self.limits)
    }
}

fn require_lf(input: &[u8], index: usize) -> Result<(), Error> {
    if input.get(index.saturating_add(1)) == Some(&b'\n') {
        Ok(())
    } else {
        Err(Error::InvalidCsv(CsvErrorKind::LoneLineBreak))
    }
}

fn push_field_byte(field: &mut Vec<u8>, byte: u8, limits: Limits) -> Result<(), Error> {
    if field.len() >= limits.max_cell_bytes {
        return Err(Error::CellTooLarge);
    }
    field.push(byte);
    Ok(())
}

fn finish_field(row: &mut Vec<String>, field: &mut Vec<u8>) -> Result<(), Error> {
    let bytes = core::mem::take(field);
    let text = String::from_utf8(bytes).map_err(|_| Error::InvalidUtf8)?;
    row.push(text);
    Ok(())
}

fn finish_row(
    parsed: &mut Vec<Vec<String>>,
    row: &mut Vec<String>,
    limits: Limits,
) -> Result<(), Error> {
    let allowed = limits.max_rows.saturating_add(1);
    if parsed.len() >= allowed {
        return Err(Error::TooManyRows);
    }
    parsed.push(core::mem::take(row));
    Ok(())
}

fn build_table(parsed: Vec<Vec<String>>, limits: Limits) -> Result<Table, Error> {
    // Consume the header instead of removing index zero from the row vector.
    // `Vec::remove(0)` shifts every remaining row even though this function
    // immediately consumes them; at the configured row ceiling that was a
    // large, avoidable pointer move on every CSV import.
    let mut parsed = parsed.into_iter();
    let Some(headers) = parsed.next() else {
        return Err(Error::InvalidCsv(CsvErrorKind::Empty));
    };
    if headers.is_empty() || headers.iter().any(String::is_empty) {
        return Err(Error::InvalidCsv(CsvErrorKind::EmptyHeader));
    }
    if headers.len() > limits.max_columns {
        return Err(Error::TooManyColumns);
    }
    for (index, header) in headers.iter().enumerate() {
        if headers.iter().take(index).any(|prior| prior == header) {
            return Err(Error::InvalidCsv(CsvErrorKind::DuplicateHeader));
        }
    }
    let row_count = parsed.len();
    let cells = row_count
        .checked_mul(headers.len())
        .ok_or(Error::TooManyCells)?;
    if cells > limits.max_cells {
        return Err(Error::TooManyCells);
    }
    if cells > limits.max_total_lineage || (cells > 0 && limits.max_lineage_per_cell == 0) {
        return Err(Error::LineageTooLarge);
    }
    let mut rows = Vec::with_capacity(row_count);
    for (row_index, values) in parsed.enumerate() {
        if values.len() != headers.len() {
            return Err(Error::InvalidCsv(CsvErrorKind::RaggedRow));
        }
        let input_row =
            u32::try_from(row_index.saturating_add(1)).map_err(|_| Error::TooManyRows)?;
        let mut row = Vec::with_capacity(values.len());
        for (column_index, value) in values.into_iter().enumerate() {
            let input_column = u32::try_from(column_index).map_err(|_| Error::TooManyColumns)?;
            row.push(SourcedCell::new(
                value,
                vec![CellRef {
                    input_row,
                    input_column,
                }],
            ));
        }
        rows.push(row);
    }
    Ok(Table { headers, rows })
}

pub(crate) fn write(table: &SourcedTable, limits: Limits) -> Result<Vec<u8>, Error> {
    validate_output_shape(table, limits)?;
    let mut output = Vec::new();
    write_row(
        &mut output,
        table.headers.iter().map(String::as_str),
        limits,
    )?;
    for row in &table.rows {
        write_row(&mut output, row.iter().map(SourcedCell::value), limits)?;
    }
    Ok(output)
}

fn validate_output_shape(table: &SourcedTable, limits: Limits) -> Result<(), Error> {
    if table.headers.is_empty() || table.headers.len() > limits.max_columns {
        return Err(Error::TooManyColumns);
    }
    if table.rows.len() > limits.max_rows {
        return Err(Error::TooManyRows);
    }
    let cells = table
        .rows
        .len()
        .checked_mul(table.headers.len())
        .ok_or(Error::TooManyCells)?;
    if cells > limits.max_cells
        || table
            .rows
            .iter()
            .any(|row| row.len() != table.headers.len())
    {
        return Err(Error::TooManyCells);
    }
    Ok(())
}

fn write_row<'a>(
    output: &mut Vec<u8>,
    cells: impl Iterator<Item = &'a str>,
    limits: Limits,
) -> Result<(), Error> {
    for (index, cell) in cells.enumerate() {
        if index > 0 {
            append(output, b",", limits)?;
        }
        if cell.bytes().any(is_disallowed_control) {
            return Err(Error::InvalidCsv(CsvErrorKind::DisallowedControl));
        }
        let safe = spreadsheet_safe(cell);
        if safe.len() > limits.max_cell_bytes.saturating_add(1) {
            return Err(Error::CellTooLarge);
        }
        if safe
            .bytes()
            .any(|byte| matches!(byte, b',' | b'"' | b'\r' | b'\n'))
        {
            append(output, b"\"", limits)?;
            for byte in safe.bytes() {
                if byte == b'"' {
                    append(output, b"\"\"", limits)?;
                } else {
                    append(output, &[byte], limits)?;
                }
            }
            append(output, b"\"", limits)?;
        } else {
            append(output, safe.as_bytes(), limits)?;
        }
    }
    append(output, b"\r\n", limits)
}

fn append(output: &mut Vec<u8>, bytes: &[u8], limits: Limits) -> Result<(), Error> {
    if output.len().saturating_add(bytes.len()) > limits.max_output_bytes {
        return Err(Error::OutputTooLarge);
    }
    output.extend_from_slice(bytes);
    Ok(())
}

const fn is_disallowed_control(byte: u8) -> bool {
    matches!(byte, 0..=9 | 11..=12 | 14..=31 | 127)
}

fn spreadsheet_safe(value: &str) -> std::borrow::Cow<'_, str> {
    let trimmed = value.trim_start_matches(is_formula_prefix_padding);
    let starts_control = value
        .as_bytes()
        .first()
        .is_some_and(|byte| matches!(byte, b'\t' | b'\r' | b'\n'));
    let starts_formula = trimmed
        .as_bytes()
        .first()
        .is_some_and(|byte| matches!(byte, b'=' | b'+' | b'-' | b'@'));
    if (!starts_control && !starts_formula) || value.starts_with('\'') {
        return std::borrow::Cow::Borrowed(value);
    }
    let is_number = Decimal::parse(trimmed).is_ok();
    if is_number {
        std::borrow::Cow::Borrowed(value)
    } else {
        std::borrow::Cow::Owned(format!("'{value}"))
    }
}

fn is_formula_prefix_padding(character: char) -> bool {
    character.is_whitespace()
        || matches!(character, '\u{200b}' | '\u{200c}' | '\u{200d}' | '\u{feff}')
}

#[cfg(test)]
mod tests {
    use super::{is_disallowed_control, spreadsheet_safe};

    #[test]
    fn the_rfc_text_guard_rejects_ascii_controls_but_keeps_record_breaks() {
        for byte in [0, 1, 9, 11, 12, 31, 127] {
            assert!(is_disallowed_control(byte), "{byte}");
        }
        for byte in [b' ', b',', b'"', b'\r', b'\n', 128] {
            assert!(!is_disallowed_control(byte), "{byte}");
        }
    }

    #[test]
    fn formula_hardening_is_idempotent_and_preserves_exact_numbers() {
        for (input, safe) in [
            ("=1+1", "'=1+1"),
            ("  @cmd", "'  @cmd"),
            ("-1.25", "-1.25"),
            ("+1.25", "+1.25"),
            ("'already safe", "'already safe"),
        ] {
            assert_eq!(spreadsheet_safe(input), safe);
            assert_eq!(spreadsheet_safe(safe), safe);
        }
    }
}
