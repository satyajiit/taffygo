// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{core_properties, root_relationships};
use crate::sink::TextSink;
use crate::xml::{escaped_text, validate_text, DECLARATION};
use crate::zip::{self, Entry};
use crate::{Deck, FileError, InputIssue, LimitKind, Limits, Slide};

const DRAWING_NS: &str = "http://schemas.openxmlformats.org/drawingml/2006/main";
const OFFICE_REL_NS: &str = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
const PRESENTATION_NS: &str = "http://schemas.openxmlformats.org/presentationml/2006/main";

pub(crate) fn generate(deck: &Deck, limits: Limits) -> Result<Vec<u8>, FileError> {
    preflight(deck, limits)?;
    let mut entries = vec![
        Entry::new(
            "[Content_Types].xml",
            content_types(deck.slides.len(), limits.max_output_bytes)?,
        ),
        Entry::new(
            "_rels/.rels",
            root_relationships("ppt/presentation.xml", limits.max_output_bytes)?,
        ),
        Entry::new(
            "docProps/core.xml",
            core_properties(&deck.title, limits.max_output_bytes)?,
        ),
        Entry::new(
            "ppt/_rels/presentation.xml.rels",
            presentation_relationships(deck.slides.len(), limits.max_output_bytes)?,
        ),
        Entry::new(
            "ppt/presentation.xml",
            presentation(deck.slides.len(), limits.max_output_bytes)?,
        ),
        Entry::new(
            "ppt/slideLayouts/_rels/slideLayout1.xml.rels",
            layout_relationships(limits.max_output_bytes)?,
        ),
        Entry::new(
            "ppt/slideLayouts/slideLayout1.xml",
            slide_layout(limits.max_output_bytes)?,
        ),
        Entry::new(
            "ppt/slideMasters/_rels/slideMaster1.xml.rels",
            master_relationships(limits.max_output_bytes)?,
        ),
        Entry::new(
            "ppt/slideMasters/slideMaster1.xml",
            slide_master(limits.max_output_bytes)?,
        ),
        Entry::new("ppt/theme/theme1.xml", theme(limits.max_output_bytes)?),
    ];
    for (index, slide) in deck.slides.iter().enumerate() {
        let number = index.saturating_add(1);
        entries.push(Entry::new(
            format!("ppt/slides/_rels/slide{number}.xml.rels"),
            slide_relationships(limits.max_output_bytes)?,
        ));
        entries.push(Entry::new(
            format!("ppt/slides/slide{number}.xml"),
            slide_part(slide, limits.max_output_bytes)?,
        ));
    }
    zip::write(entries, limits)
}

fn preflight(deck: &Deck, limits: Limits) -> Result<(), FileError> {
    if deck.title.trim().is_empty() || deck.slides.is_empty() {
        return Err(FileError::InvalidInput(InputIssue::EmptyRequiredValue));
    }
    validate_text(&deck.title)?;
    if deck.slides.len() > limits.max_slides {
        return Err(limit(LimitKind::Slides, limits.max_slides));
    }
    let part_count = deck
        .slides
        .len()
        .checked_mul(2)
        .and_then(|value| value.checked_add(10))
        .ok_or_else(|| limit(LimitKind::PackageParts, limits.max_package_parts))?;
    if part_count > limits.max_package_parts {
        return Err(limit(LimitKind::PackageParts, limits.max_package_parts));
    }
    let mut text_bytes = deck.title.len();
    for slide in &deck.slides {
        if slide.title.trim().is_empty() {
            return Err(FileError::InvalidInput(InputIssue::EmptyRequiredValue));
        }
        text_bytes = add_text(text_bytes, &slide.title, limits)?;
        if slide.body.len() > limits.max_lines_per_page {
            return Err(limit(LimitKind::Lines, limits.max_lines_per_page));
        }
        for line in &slide.body {
            text_bytes = add_text(text_bytes, line, limits)?;
        }
    }
    Ok(())
}

fn content_types(slide_count: usize, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/><Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/><Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/><Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/><Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>")?;
    for index in 1..=slide_count {
        xml.push_str("<Override PartName=\"/ppt/slides/slide")?;
        xml.decimal(index)?;
        xml.push_str(".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>")?;
    }
    xml.push_str("</Types>")?;
    Ok(xml.into_bytes())
}

fn presentation(slide_count: usize, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    namespaces(&mut xml, "p:presentation")?;
    xml.push_str("<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst><p:sldIdLst>")?;
    for index in 1..=slide_count {
        xml.push_str("<p:sldId id=\"")?;
        xml.decimal(255usize.saturating_add(index))?;
        xml.push_str("\" r:id=\"rId")?;
        xml.decimal(index.saturating_add(1))?;
        xml.push_str("\"/>")?;
    }
    xml.push_str("</p:sldIdLst><p:sldSz cx=\"12192000\" cy=\"6858000\" type=\"screen16x9\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>")?;
    Ok(xml.into_bytes())
}

fn presentation_relationships(slide_count: usize, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = relationships(maximum)?;
    relationship(&mut xml, 1, "slideMaster", "slideMasters/slideMaster1.xml")?;
    for index in 1..=slide_count {
        relationship(
            &mut xml,
            index.saturating_add(1),
            "slide",
            &format!("slides/slide{index}.xml"),
        )?;
    }
    xml.push_str("</Relationships>")?;
    Ok(xml.into_bytes())
}

fn slide_part(slide: &Slide, maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    namespaces(&mut xml, "p:sld")?;
    xml.push_str("<p:cSld><p:spTree>")?;
    group_shape(&mut xml)?;
    text_shape(
        &mut xml,
        2,
        "Title",
        457_200,
        274_320,
        11_277_600,
        914_400,
        &[&slide.title],
        2_800,
        true,
    )?;
    let body: Vec<&str> = slide.body.iter().map(String::as_str).collect();
    text_shape(
        &mut xml, 3, "Body", 685_800, 1_463_040, 10_820_400, 4_572_000, &body, 1_800, false,
    )?;
    xml.push_str("</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>")?;
    Ok(xml.into_bytes())
}

#[allow(clippy::too_many_arguments)]
fn text_shape(
    xml: &mut TextSink,
    id: usize,
    name: &str,
    x: usize,
    y: usize,
    cx: usize,
    cy: usize,
    lines: &[&str],
    font_size: usize,
    bold: bool,
) -> Result<(), FileError> {
    xml.push_str("<p:sp><p:nvSpPr><p:cNvPr id=\"")?;
    xml.decimal(id)?;
    xml.push_str("\" name=\"")?;
    xml.push_str(name)?;
    xml.push_str("\"/><p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x=\"")?;
    xml.decimal(x)?;
    xml.push_str("\" y=\"")?;
    xml.decimal(y)?;
    xml.push_str("\"/><a:ext cx=\"")?;
    xml.decimal(cx)?;
    xml.push_str("\" cy=\"")?;
    xml.decimal(cy)?;
    xml.push_str("\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/><a:ln><a:noFill/></a:ln></p:spPr><p:txBody><a:bodyPr wrap=\"square\"/><a:lstStyle/>")?;
    if lines.is_empty() {
        xml.push_str("<a:p><a:endParaRPr lang=\"en-US\"/></a:p>")?;
    } else {
        for line in lines {
            xml.push_str("<a:p><a:r><a:rPr lang=\"en-US\" sz=\"")?;
            xml.decimal(font_size)?;
            if bold {
                xml.push_str("\" b=\"1")?;
            }
            xml.push_str("\"/><a:t>")?;
            escaped_text(xml, line)?;
            xml.push_str("</a:t></a:r><a:endParaRPr lang=\"en-US\"/></a:p>")?;
        }
    }
    xml.push_str("</p:txBody></p:sp>")
}

fn slide_relationships(maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = relationships(maximum)?;
    relationship(
        &mut xml,
        1,
        "slideLayout",
        "../slideLayouts/slideLayout1.xml",
    )?;
    xml.push_str("</Relationships>")?;
    Ok(xml.into_bytes())
}

fn slide_layout(maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    namespaces(&mut xml, "p:sldLayout")?;
    xml.push_str("<p:cSld name=\"Blank\"><p:spTree>")?;
    group_shape(&mut xml)?;
    xml.push_str(
        "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>",
    )?;
    Ok(xml.into_bytes())
}

fn layout_relationships(maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = relationships(maximum)?;
    relationship(
        &mut xml,
        1,
        "slideMaster",
        "../slideMasters/slideMaster1.xml",
    )?;
    xml.push_str("</Relationships>")?;
    Ok(xml.into_bytes())
}

fn slide_master(maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    namespaces(&mut xml, "p:sldMaster")?;
    xml.push_str("<p:cSld name=\"TaffyGo\"><p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill><a:effectLst/></p:bgPr></p:bg><p:spTree>")?;
    group_shape(&mut xml)?;
    xml.push_str("</p:spTree></p:cSld><p:clrMap accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" bg1=\"lt1\" bg2=\"lt2\" folHlink=\"folHlink\" hlink=\"hlink\" tx1=\"dk1\" tx2=\"dk2\"/><p:sldLayoutIdLst><p:sldLayoutId id=\"1\" r:id=\"rId1\"/></p:sldLayoutIdLst><p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles></p:sldMaster>")?;
    Ok(xml.into_bytes())
}

fn master_relationships(maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = relationships(maximum)?;
    relationship(
        &mut xml,
        1,
        "slideLayout",
        "../slideLayouts/slideLayout1.xml",
    )?;
    relationship(&mut xml, 2, "theme", "../theme/theme1.xml")?;
    xml.push_str("</Relationships>")?;
    Ok(xml.into_bytes())
}

fn theme(maximum: usize) -> Result<Vec<u8>, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str("<a:theme xmlns:a=\"")?;
    xml.push_str(DRAWING_NS)?;
    xml.push_str("\" name=\"TaffyGo\"><a:themeElements><a:clrScheme name=\"TaffyGo\"><a:dk1><a:srgbClr val=\"111827\"/></a:dk1><a:lt1><a:srgbClr val=\"FFFFFF\"/></a:lt1><a:dk2><a:srgbClr val=\"334155\"/></a:dk2><a:lt2><a:srgbClr val=\"F8FAFC\"/></a:lt2><a:accent1><a:srgbClr val=\"7C3AED\"/></a:accent1><a:accent2><a:srgbClr val=\"0F766E\"/></a:accent2><a:accent3><a:srgbClr val=\"B45309\"/></a:accent3><a:accent4><a:srgbClr val=\"1D4ED8\"/></a:accent4><a:accent5><a:srgbClr val=\"BE123C\"/></a:accent5><a:accent6><a:srgbClr val=\"4D7C0F\"/></a:accent6><a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink><a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink></a:clrScheme><a:fontScheme name=\"TaffyGo\"><a:majorFont><a:latin typeface=\"Arial\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont><a:minorFont><a:latin typeface=\"Arial\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont></a:fontScheme><a:fmtScheme name=\"TaffyGo\"><a:fillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:fillStyleLst><a:lnStyleLst><a:ln w=\"6350\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln></a:lnStyleLst><a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst><a:bgFillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>")?;
    Ok(xml.into_bytes())
}

fn namespaces(xml: &mut TextSink, root: &str) -> Result<(), FileError> {
    xml.push_str("<")?;
    xml.push_str(root)?;
    xml.push_str(" xmlns:a=\"")?;
    xml.push_str(DRAWING_NS)?;
    xml.push_str("\" xmlns:r=\"")?;
    xml.push_str(OFFICE_REL_NS)?;
    xml.push_str("\" xmlns:p=\"")?;
    xml.push_str(PRESENTATION_NS)?;
    xml.push_str("\">")
}

fn group_shape(xml: &mut TextSink) -> Result<(), FileError> {
    xml.push_str("<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>")
}

fn relationships(maximum: usize) -> Result<TextSink, FileError> {
    let mut xml = TextSink::new(maximum);
    xml.push_str(DECLARATION)?;
    xml.push_str(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">",
    )?;
    Ok(xml)
}

fn relationship(xml: &mut TextSink, id: usize, kind: &str, target: &str) -> Result<(), FileError> {
    xml.push_str("<Relationship Id=\"rId")?;
    xml.decimal(id)?;
    xml.push_str("\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/")?;
    xml.push_str(kind)?;
    xml.push_str("\" Target=\"")?;
    xml.push_str(target)?;
    xml.push_str("\"/>")
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
