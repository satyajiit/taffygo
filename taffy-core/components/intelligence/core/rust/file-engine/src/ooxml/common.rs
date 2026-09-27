// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::sink::TextSink;
use crate::xml::{escaped_text, DECLARATION};
use crate::FileError;

pub(crate) fn root_relationships(
    office_target: &str,
    maximum: usize,
) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">",
    )?;
    xml.push_str(
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"",
    )?;
    xml.push_str(office_target)?;
    xml.push_str("\"/>")?;
    xml.push_str(
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>",
    )?;
    xml.push_str("</Relationships>")?;
    Ok(xml.into_bytes())
}

pub(crate) fn core_properties(title: &str, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str(
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">",
    )?;
    xml.push_str("<dc:title>")?;
    escaped_text(&mut xml, title)?;
    xml.push_str("</dc:title><dc:creator>TaffyGo</dc:creator>")?;
    xml.push_str(
        "<dcterms:created xsi:type=\"dcterms:W3CDTF\">2000-01-01T00:00:00Z</dcterms:created>",
    )?;
    xml.push_str(
        "<dcterms:modified xsi:type=\"dcterms:W3CDTF\">2000-01-01T00:00:00Z</dcterms:modified>",
    )?;
    xml.push_str("</cp:coreProperties>")?;
    Ok(xml.into_bytes())
}
