// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::BTreeSet;

use super::{core_properties, root_relationships};
use crate::sink::TextSink;
use crate::xml::{escaped_attribute, escaped_text, validate_text, DECLARATION};
use crate::zip::{self, Entry};
use crate::{Cell, FileError, InputIssue, LimitKind, Limits, Sheet, Workbook};

pub(crate) fn generate(workbook: &Workbook, limits: Limits) -> Result<Vec<u8>, FileError> {
    preflight(workbook, limits)?;
    let mut entries = vec![
        Entry::new(
            "[Content_Types].xml",
            content_types(workbook.sheets.len(), limits.max_output_bytes)?,
        ),
        Entry::new(
            "_rels/.rels",
            root_relationships("xl/workbook.xml", limits.max_output_bytes)?,
        ),
        Entry::new(
            "docProps/core.xml",
            core_properties(&workbook.title, limits.max_output_bytes)?,
        ),
        Entry::new(
            "xl/_rels/workbook.xml.rels",
            workbook_relationships(workbook.sheets.len(), limits.max_output_bytes)?,
        ),
        Entry::new(
            "xl/workbook.xml",
            workbook_part(workbook, limits.max_output_bytes)?,
        ),
    ];
    for (index, sheet) in workbook.sheets.iter().enumerate() {
        entries.push(Entry::new(
            format!("xl/worksheets/sheet{}.xml", index.saturating_add(1)),
            worksheet(sheet, limits.max_output_bytes)?,
        ));
    }
    zip::write(entries, limits)
}

fn preflight(workbook: &Workbook, limits: Limits) -> Result<(), FileError> {
    if workbook.title.trim().is_empty() || workbook.sheets.is_empty() {
        return Err(FileError::InvalidInput(InputIssue::EmptyRequiredValue));
    }
    validate_text(&workbook.title)?;
    if workbook.sheets.len() > limits.max_sheets {
        return Err(limit(LimitKind::Sheets, limits.max_sheets));
    }
    let mut names = BTreeSet::new();
    let mut text_bytes = workbook.title.len();
    let mut cell_count = 0usize;
    for sheet in &workbook.sheets {
        validate_sheet_name(&sheet.name)?;
        if !names.insert(sheet.name.to_lowercase()) {
            return Err(FileError::InvalidInput(InputIssue::InvalidSheetName));
        }
        text_bytes = add_text(text_bytes, sheet.name.len(), limits)?;
        if sheet.rows.len() > limits.max_rows {
            return Err(limit(LimitKind::Rows, limits.max_rows));
        }
        for row in &sheet.rows {
            if row.len() > limits.max_columns {
                return Err(limit(LimitKind::Columns, limits.max_columns));
            }
            for cell in row {
                match cell {
                    Cell::Blank => {}
                    Cell::Text(value) => {
                        validate_text(value)?;
                        text_bytes = add_text(text_bytes, value.len(), limits)?;
                        cell_count = add_cell(cell_count, limits)?;
                    }
                    Cell::Number(value) => {
                        if !value.is_finite() {
                            return Err(FileError::InvalidInput(InputIssue::NonFiniteNumber));
                        }
                        cell_count = add_cell(cell_count, limits)?;
                    }
                    Cell::Boolean(_) => cell_count = add_cell(cell_count, limits)?,
                }
            }
        }
    }
    Ok(())
}

fn validate_sheet_name(name: &str) -> Result<(), FileError> {
    validate_text(name)?;
    if name.trim().is_empty()
        || name.chars().count() > 31
        || name.starts_with('\'')
        || name.ends_with('\'')
        || name
            .chars()
            .any(|character| matches!(character, ':' | '\\' | '/' | '?' | '*' | '[' | ']'))
    {
        return Err(FileError::InvalidInput(InputIssue::InvalidSheetName));
    }
    Ok(())
}

fn content_types(sheet_count: usize, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>")?;
    for index in 1..=sheet_count {
        xml.push_str("<Override PartName=\"/xl/worksheets/sheet")?;
        xml.decimal(index)?;
        xml.push_str(".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")?;
    }
    xml.push_str("</Types>")?;
    Ok(xml.into_bytes())
}

fn workbook_part(workbook: &Workbook, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str("<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>")?;
    for (index, sheet) in workbook.sheets.iter().enumerate() {
        let number = index.saturating_add(1);
        xml.push_str("<sheet name=\"")?;
        escaped_attribute(&mut xml, &sheet.name)?;
        xml.push_str("\" sheetId=\"")?;
        xml.decimal(number)?;
        xml.push_str("\" r:id=\"rId")?;
        xml.decimal(number)?;
        xml.push_str("\"/>")?;
    }
    xml.push_str("</sheets></workbook>")?;
    Ok(xml.into_bytes())
}

fn workbook_relationships(sheet_count: usize, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">",
    )?;
    for index in 1..=sheet_count {
        xml.push_str("<Relationship Id=\"rId")?;
        xml.decimal(index)?;
        xml.push_str("\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet")?;
        xml.decimal(index)?;
        xml.push_str(".xml\"/>")?;
    }
    xml.push_str("</Relationships>")?;
    Ok(xml.into_bytes())
}

fn worksheet(sheet: &Sheet, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>")?;
    for (row_index, row) in sheet.rows.iter().enumerate() {
        let row_number = row_index.saturating_add(1);
        xml.push_str("<row r=\"")?;
        xml.decimal(row_number)?;
        xml.push_str("\">")?;
        for (column_index, cell) in row.iter().enumerate() {
            if matches!(cell, Cell::Blank) {
                continue;
            }
            xml.push_str("<c r=\"")?;
            xml.push_str(&cell_reference(column_index, row_number))?;
            match cell {
                Cell::Blank => {}
                Cell::Text(value) => {
                    xml.push_str("\" t=\"inlineStr\"><is><t xml:space=\"preserve\">")?;
                    escaped_text(&mut xml, value)?;
                    xml.push_str("</t></is></c>")?;
                }
                Cell::Number(value) => {
                    xml.push_str("\"><v>")?;
                    if *value == 0.0 {
                        xml.push_str("0")?;
                    } else {
                        xml.push_str(&value.to_string())?;
                    }
                    xml.push_str("</v></c>")?;
                }
                Cell::Boolean(value) => {
                    xml.push_str("\" t=\"b\"><v>")?;
                    xml.push_str(if *value { "1" } else { "0" })?;
                    xml.push_str("</v></c>")?;
                }
            }
        }
        xml.push_str("</row>")?;
    }
    xml.push_str("</sheetData></worksheet>")?;
    Ok(xml.into_bytes())
}

fn cell_reference(column_index: usize, row_number: usize) -> String {
    let mut column = column_index.saturating_add(1);
    let mut letters = Vec::with_capacity(3);
    while column > 0 {
        let remainder = (column.saturating_sub(1)) % 26;
        let letter = u8::try_from(remainder)
            .ok()
            .and_then(|value| value.checked_add(b'A'))
            .map_or('A', char::from);
        letters.push(letter);
        column = column.saturating_sub(1) / 26;
    }
    let mut result: String = letters.into_iter().rev().collect();
    result.push_str(&row_number.to_string());
    result
}

fn add_text(current: usize, additional: usize, limits: Limits) -> Result<usize, FileError> {
    let next = current
        .checked_add(additional)
        .ok_or_else(|| limit(LimitKind::TextBytes, limits.max_text_bytes))?;
    if next > limits.max_text_bytes {
        return Err(limit(LimitKind::TextBytes, limits.max_text_bytes));
    }
    Ok(next)
}

fn add_cell(current: usize, limits: Limits) -> Result<usize, FileError> {
    let next = current
        .checked_add(1)
        .ok_or_else(|| limit(LimitKind::Cells, limits.max_cells))?;
    if next > limits.max_cells {
        return Err(limit(LimitKind::Cells, limits.max_cells));
    }
    Ok(next)
}

fn limit(kind: LimitKind, maximum: usize) -> FileError {
    FileError::LimitExceeded { kind, maximum }
}
