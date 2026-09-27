// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::sink::ByteSink;
use crate::{
    FileError, Format, InputIssue, LimitKind, Limits, PackageIssue, PdfDocument, Validation,
};

const HEADER: &[u8] = b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n";

pub(crate) fn generate(document: &PdfDocument, limits: Limits) -> Result<Vec<u8>, FileError> {
    preflight(document, limits)?;
    let page_count = document.pages.len();
    let object_count = page_count
        .checked_mul(2)
        .and_then(|value| value.checked_add(4))
        .ok_or_else(|| output_limit(limits))?;
    let mut contents = Vec::with_capacity(page_count);
    for page in &document.pages {
        let mut stream = ByteSink::new(limits.max_output_bytes);
        stream.extend(b"BT\n/F1 12 Tf\n72 770 Td\n")?;
        for line in &page.lines {
            stream.push(b'(')?;
            pdf_literal(&mut stream, line)?;
            stream.extend(b") Tj\n0 -16 Td\n")?;
        }
        stream.extend(b"ET\n")?;
        contents.push(stream.into_bytes());
    }

    let mut sink = ByteSink::new(limits.max_output_bytes);
    sink.extend(HEADER)?;
    let mut offsets = Vec::with_capacity(object_count.saturating_add(1));
    offsets.push(0usize);
    offsets.push(write_object(
        &mut sink,
        1,
        b"<< /Type /Catalog /Pages 2 0 R >>",
    )?);

    let mut pages = ByteSink::new(limits.max_output_bytes);
    pages.extend(b"<< /Type /Pages /Count ")?;
    pages.decimal(page_count)?;
    pages.extend(b" /Kids [")?;
    for index in 0..page_count {
        pages.decimal(5usize.saturating_add(index.saturating_mul(2)))?;
        pages.extend(b" 0 R ")?;
    }
    pages.extend(b"] >>")?;
    offsets.push(write_object(&mut sink, 2, &pages.into_bytes())?);
    offsets.push(write_object(
        &mut sink,
        3,
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
    )?);

    let mut info = ByteSink::new(limits.max_output_bytes);
    info.extend(b"<< /Title (")?;
    pdf_literal(&mut info, &document.title)?;
    info.extend(
        b") /Producer (TaffyGo) /CreationDate (D:20000101000000Z) /ModDate (D:20000101000000Z) >>",
    )?;
    offsets.push(write_object(&mut sink, 4, &info.into_bytes())?);

    for (index, content) in contents.iter().enumerate() {
        let page_id = 5usize.saturating_add(index.saturating_mul(2));
        let content_id = page_id.saturating_add(1);
        let mut page = ByteSink::new(limits.max_output_bytes);
        page.extend(b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents ")?;
        page.decimal(content_id)?;
        page.extend(b" 0 R >>")?;
        offsets.push(write_object(&mut sink, page_id, &page.into_bytes())?);

        let mut stream = ByteSink::new(limits.max_output_bytes);
        stream.extend(b"<< /Length ")?;
        stream.decimal(content.len())?;
        stream.extend(b" >>\nstream\n")?;
        stream.extend(content)?;
        stream.extend(b"endstream")?;
        offsets.push(write_object(&mut sink, content_id, &stream.into_bytes())?);
    }

    let xref_offset = sink.position();
    let size = object_count.saturating_add(1);
    sink.extend(b"xref\n0 ")?;
    sink.decimal(size)?;
    sink.extend(b"\n0000000000 65535 f \n")?;
    for offset in offsets.iter().skip(1) {
        if *offset >= 10_000_000_000usize {
            return Err(output_limit(limits));
        }
        sink.extend(format!("{offset:010} 00000 n \n").as_bytes())?;
    }
    sink.extend(b"trailer\n<< /Size ")?;
    sink.decimal(size)?;
    sink.extend(b" /Root 1 0 R /Info 4 0 R >>\nstartxref\n")?;
    sink.decimal(xref_offset)?;
    sink.extend(b"\n%%EOF\n")?;
    Ok(sink.into_bytes())
}

pub(crate) fn validate(bytes: &[u8], limits: Limits) -> Result<Validation, FileError> {
    if bytes.len() > limits.max_output_bytes {
        return Err(output_limit(limits));
    }
    if !bytes.starts_with(HEADER) || forbidden(bytes) {
        return Err(invalid_pdf());
    }
    let marker = b"\nstartxref\n";
    let marker_offset = bytes
        .windows(marker.len())
        .rposition(|window| window == marker)
        .ok_or_else(invalid_pdf)?;
    let number_start = marker_offset.saturating_add(marker.len());
    let remainder = bytes.get(number_start..).ok_or_else(invalid_pdf)?;
    let newline = remainder
        .iter()
        .position(|byte| *byte == b'\n')
        .ok_or_else(invalid_pdf)?;
    let xref_offset = parse_decimal(remainder.get(..newline).ok_or_else(invalid_pdf)?)?;
    if remainder.get(newline..) != Some(b"\n%%EOF\n") {
        return Err(invalid_pdf());
    }
    let xref = bytes
        .get(xref_offset..marker_offset)
        .ok_or_else(invalid_pdf)?;
    let Some(after_header) = xref.strip_prefix(b"xref\n0 ") else {
        return Err(invalid_pdf());
    };
    let count_end = after_header
        .iter()
        .position(|byte| *byte == b'\n')
        .ok_or_else(invalid_pdf)?;
    let count = parse_decimal(after_header.get(..count_end).ok_or_else(invalid_pdf)?)?;
    if count < 7 || count.saturating_sub(5) % 2 != 0 {
        return Err(invalid_pdf());
    }
    let page_count = count.saturating_sub(5) / 2;
    if page_count > limits.max_pages {
        return Err(FileError::LimitExceeded {
            kind: LimitKind::Pages,
            maximum: limits.max_pages,
        });
    }
    let records_start = count_end.saturating_add(1);
    let records_len = count.checked_mul(20).ok_or_else(invalid_pdf)?;
    let records = after_header
        .get(records_start..records_start.saturating_add(records_len))
        .ok_or_else(invalid_pdf)?;
    if records.get(..20) != Some(b"0000000000 65535 f \n") {
        return Err(invalid_pdf());
    }
    let mut offsets = Vec::with_capacity(count);
    offsets.push(0usize);
    let mut prior = 0usize;
    for object in 1..count {
        let start = object.saturating_mul(20);
        let record = records
            .get(start..start.saturating_add(20))
            .ok_or_else(invalid_pdf)?;
        if record.get(10..) != Some(b" 00000 n \n") {
            return Err(invalid_pdf());
        }
        let offset = parse_decimal(record.get(..10).ok_or_else(invalid_pdf)?)?;
        if offset <= prior || offset >= xref_offset {
            return Err(invalid_pdf());
        }
        offsets.push(offset);
        prior = offset;
    }
    let trailer = after_header
        .get(records_start.saturating_add(records_len)..)
        .ok_or_else(invalid_pdf)?;
    let expected = format!("trailer\n<< /Size {count} /Root 1 0 R /Info 4 0 R >>");
    if trailer != expected.as_bytes() || offsets.get(1).copied() != Some(HEADER.len()) {
        return Err(invalid_pdf());
    }
    validate_objects(bytes, &offsets, xref_offset, page_count)?;
    Ok(Validation {
        format: Format::Pdf,
        byte_len: bytes.len(),
        structural_parts: count.saturating_sub(1),
        logical_units: page_count,
    })
}

fn validate_objects(
    bytes: &[u8],
    offsets: &[usize],
    xref_offset: usize,
    pages: usize,
) -> Result<(), FileError> {
    for id in 1..offsets.len() {
        let start = offsets.get(id).copied().ok_or_else(invalid_pdf)?;
        let end = offsets
            .get(id.saturating_add(1))
            .copied()
            .unwrap_or(xref_offset);
        let object = bytes.get(start..end).ok_or_else(invalid_pdf)?;
        let prefix = format!("{id} 0 obj\n");
        if !object.starts_with(prefix.as_bytes()) || !object.ends_with(b"\nendobj\n") {
            return Err(invalid_pdf());
        }
    }
    let catalog = object_body(bytes, offsets, xref_offset, 1)?;
    let page_tree = object_body(bytes, offsets, xref_offset, 2)?;
    if catalog != b"<< /Type /Catalog /Pages 2 0 R >>"
        || !page_tree.starts_with(format!("<< /Type /Pages /Count {pages} /Kids [").as_bytes())
        || !object_body(bytes, offsets, xref_offset, 3)?.starts_with(b"<< /Type /Font ")
    {
        return Err(invalid_pdf());
    }
    for index in 0..pages {
        let page_id = 5usize.saturating_add(index.saturating_mul(2));
        let content_id = page_id.saturating_add(1);
        let page = object_body(bytes, offsets, xref_offset, page_id)?;
        if !page.starts_with(b"<< /Type /Page /Parent 2 0 R ")
            || !page.ends_with(format!("/Contents {content_id} 0 R >>").as_bytes())
        {
            return Err(invalid_pdf());
        }
        validate_stream(object_body(bytes, offsets, xref_offset, content_id)?)?;
    }
    Ok(())
}

fn validate_stream(body: &[u8]) -> Result<(), FileError> {
    let Some(after) = body.strip_prefix(b"<< /Length ") else {
        return Err(invalid_pdf());
    };
    let marker = b" >>\nstream\n";
    let marker_at = after
        .windows(marker.len())
        .position(|window| window == marker)
        .ok_or_else(invalid_pdf)?;
    let declared = parse_decimal(after.get(..marker_at).ok_or_else(invalid_pdf)?)?;
    let data = after
        .get(marker_at.saturating_add(marker.len())..)
        .and_then(|value| value.strip_suffix(b"endstream"))
        .ok_or_else(invalid_pdf)?;
    if data.len() != declared {
        return Err(invalid_pdf());
    }
    Ok(())
}

fn object_body<'a>(
    bytes: &'a [u8],
    offsets: &[usize],
    xref: usize,
    id: usize,
) -> Result<&'a [u8], FileError> {
    let start = offsets.get(id).copied().ok_or_else(invalid_pdf)?;
    let end = offsets.get(id.saturating_add(1)).copied().unwrap_or(xref);
    let object = bytes.get(start..end).ok_or_else(invalid_pdf)?;
    let prefix = format!("{id} 0 obj\n");
    object
        .strip_prefix(prefix.as_bytes())
        .and_then(|value| value.strip_suffix(b"\nendobj\n"))
        .ok_or_else(invalid_pdf)
}

fn preflight(document: &PdfDocument, limits: Limits) -> Result<(), FileError> {
    if document.title.trim().is_empty() || document.pages.is_empty() {
        return Err(FileError::InvalidInput(InputIssue::EmptyRequiredValue));
    }
    if document.pages.len() > limits.max_pages {
        return Err(FileError::LimitExceeded {
            kind: LimitKind::Pages,
            maximum: limits.max_pages,
        });
    }
    let mut text = 0usize;
    for value in std::iter::once(&document.title)
        .chain(document.pages.iter().flat_map(|page| page.lines.iter()))
    {
        if !value
            .chars()
            .all(|character| character == ' ' || character.is_ascii_graphic())
        {
            return Err(FileError::InvalidInput(InputIssue::UnsupportedCharacter));
        }
        text = text
            .checked_add(value.len())
            .ok_or_else(|| text_limit(limits))?;
        if text > limits.max_text_bytes {
            return Err(text_limit(limits));
        }
    }
    for page in &document.pages {
        if page.lines.len() > limits.max_lines_per_page {
            return Err(FileError::LimitExceeded {
                kind: LimitKind::Lines,
                maximum: limits.max_lines_per_page,
            });
        }
    }
    Ok(())
}

fn write_object(sink: &mut ByteSink, id: usize, body: &[u8]) -> Result<usize, FileError> {
    let offset = sink.position();
    sink.decimal(id)?;
    sink.extend(b" 0 obj\n")?;
    sink.extend(body)?;
    sink.extend(b"\nendobj\n")?;
    Ok(offset)
}

fn pdf_literal(sink: &mut ByteSink, value: &str) -> Result<(), FileError> {
    for byte in value.bytes() {
        if matches!(byte, b'\\' | b'(' | b')') {
            sink.push(b'\\')?;
        }
        sink.push(byte)?;
    }
    Ok(())
}

fn parse_decimal(bytes: &[u8]) -> Result<usize, FileError> {
    if bytes.is_empty() || !bytes.iter().all(u8::is_ascii_digit) {
        return Err(invalid_pdf());
    }
    let text = std::str::from_utf8(bytes).map_err(|_| invalid_pdf())?;
    text.parse().map_err(|_| invalid_pdf())
}

fn forbidden(bytes: &[u8]) -> bool {
    const TOKENS: [&[u8]; 7] = [
        b"/JavaScript",
        b"/OpenAction",
        b"/EmbeddedFile",
        b"/Launch",
        b"/URI",
        b"/Encrypt",
        b"/AA",
    ];
    TOKENS
        .into_iter()
        .any(|token| bytes.windows(token.len()).any(|window| window == token))
}

fn invalid_pdf() -> FileError {
    FileError::InvalidPackage(PackageIssue::InvalidPdf)
}

fn output_limit(limits: Limits) -> FileError {
    FileError::LimitExceeded {
        kind: LimitKind::OutputBytes,
        maximum: limits.max_output_bytes,
    }
}

fn text_limit(limits: Limits) -> FileError {
    FileError::LimitExceeded {
        kind: LimitKind::TextBytes,
        maximum: limits.max_text_bytes,
    }
}
