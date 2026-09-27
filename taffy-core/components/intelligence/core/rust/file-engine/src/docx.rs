// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::ooxml::{core_properties, root_relationships};
use crate::sink::TextSink;
use crate::xml::{escaped_text, validate_text, DECLARATION};
use crate::zip::{self, Entry};
use crate::{Block, Document, FileError, InputIssue, LimitKind, Limits};

pub(crate) fn generate(document: &Document, limits: Limits) -> Result<Vec<u8>, FileError> {
    preflight(document, limits)?;
    zip::write(
        vec![
            Entry::new(
                "[Content_Types].xml",
                content_types(limits.max_output_bytes)?,
            ),
            Entry::new(
                "_rels/.rels",
                root_relationships("word/document.xml", limits.max_output_bytes)?,
            ),
            Entry::new(
                "docProps/core.xml",
                core_properties(&document.title, limits.max_output_bytes)?,
            ),
            Entry::new(
                "word/document.xml",
                document_part(document, limits.max_output_bytes)?,
            ),
        ],
        limits,
    )
}

fn preflight(document: &Document, limits: Limits) -> Result<(), FileError> {
    if document.title.trim().is_empty() || document.blocks.is_empty() {
        return Err(FileError::InvalidInput(InputIssue::EmptyRequiredValue));
    }
    validate_text(&document.title)?;
    if document.blocks.len() > limits.max_blocks {
        return Err(limit(LimitKind::Blocks, limits.max_blocks));
    }
    let mut text_bytes = document.title.len();
    let mut cells = 0usize;
    for block in &document.blocks {
        match block {
            Block::Paragraph(text) => {
                text_bytes = add_text(text_bytes, text, limits)?;
            }
            Block::Heading { level, text } => {
                if !(1..=6).contains(level) {
                    return Err(FileError::InvalidInput(InputIssue::InvalidHeadingLevel));
                }
                text_bytes = add_text(text_bytes, text, limits)?;
            }
            Block::Table(rows) => {
                if rows.is_empty() || rows.len() > limits.max_rows {
                    return Err(if rows.is_empty() {
                        FileError::InvalidInput(InputIssue::EmptyRequiredValue)
                    } else {
                        limit(LimitKind::Rows, limits.max_rows)
                    });
                }
                for row in rows {
                    if row.is_empty() || row.len() > limits.max_columns {
                        return Err(if row.is_empty() {
                            FileError::InvalidInput(InputIssue::EmptyRequiredValue)
                        } else {
                            limit(LimitKind::Columns, limits.max_columns)
                        });
                    }
                    for value in row {
                        cells = cells
                            .checked_add(1)
                            .ok_or_else(|| limit(LimitKind::Cells, limits.max_cells))?;
                        if cells > limits.max_cells {
                            return Err(limit(LimitKind::Cells, limits.max_cells));
                        }
                        text_bytes = add_text(text_bytes, value, limits)?;
                    }
                }
            }
        }
    }
    Ok(())
}

fn content_types(maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>")?;
    Ok(xml.into_bytes())
}

fn document_part(document: &Document, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>")?;
    for block in &document.blocks {
        match block {
            Block::Paragraph(text) => paragraph(&mut xml, text, None)?,
            Block::Heading { level, text } => paragraph(&mut xml, text, Some(*level))?,
            Block::Table(rows) => table(&mut xml, rows)?,
        }
    }
    xml.push_str("<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>")?;
    xml.push_str("</w:body></w:document>")?;
    Ok(xml.into_bytes())
}

fn paragraph(sink: &mut TextSink, text: &str, heading: Option<u8>) -> Result<(), FileError> {
    sink.push_str("<w:p>")?;
    if let Some(level) = heading {
        sink.push_str("<w:pPr><w:outlineLvl w:val=\"")?;
        sink.decimal(usize::from(level.saturating_sub(1)))?;
        sink.push_str("\"/></w:pPr>")?;
    }
    sink.push_str("<w:r><w:t xml:space=\"preserve\">")?;
    escaped_text(sink, text)?;
    sink.push_str("</w:t></w:r></w:p>")
}

fn table(sink: &mut TextSink, rows: &[Vec<String>]) -> Result<(), FileError> {
    sink.push_str("<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr>")?;
    for row in rows {
        sink.push_str("<w:tr>")?;
        for cell in row {
            sink.push_str("<w:tc><w:tcPr><w:tcW w:w=\"0\" w:type=\"auto\"/></w:tcPr>")?;
            paragraph(sink, cell, None)?;
            sink.push_str("</w:tc>")?;
        }
        sink.push_str("</w:tr>")?;
    }
    sink.push_str("</w:tbl>")
}

fn add_text(current: usize, value: &str, limits: Limits) -> Result<usize, FileError> {
    validate_text(value)?;
    let next = current
        .checked_add(value.len())
        .ok_or_else(|| limit(LimitKind::TextBytes, limits.max_text_bytes))?;
    if next > limits.max_text_bytes {
        return Err(limit(LimitKind::TextBytes, limits.max_text_bytes));
    }
    Ok(next)
}

fn limit(kind: LimitKind, maximum: usize) -> FileError {
    FileError::LimitExceeded { kind, maximum }
}
