// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::BTreeSet;
use std::path::Path;

use crate::zip::Package;
use crate::{xml, FileError, Format, Limits, PackageIssue, Validation};

pub(crate) fn validate(
    format: Format,
    bytes: &[u8],
    limits: Limits,
) -> Result<Validation, FileError> {
    if format == Format::Pdf {
        return crate::pdf::validate(bytes, limits);
    }
    let package = Package::parse(bytes, limits)?;
    for part in &package.parts {
        if is_xml_part(part.name) {
            xml::validate(part.data)?;
            reject_active_content(part.data)?;
        }
    }
    let logical_units = match format {
        Format::Xlsx => validate_xlsx(&package)?,
        Format::Docx => validate_docx(&package)?,
        Format::Pptx => validate_pptx(&package)?,
        Format::Pdf => return Err(package_error(PackageIssue::Malformed)),
    };
    Ok(Validation {
        format,
        byte_len: bytes.len(),
        structural_parts: package.parts.len(),
        logical_units,
    })
}

fn is_xml_part(name: &str) -> bool {
    let path = Path::new(name);
    path.file_name()
        .and_then(|value| value.to_str())
        .is_some_and(|value| value.eq_ignore_ascii_case(".rels"))
        || path.extension().is_some_and(|extension| {
            extension.eq_ignore_ascii_case("xml") || extension.eq_ignore_ascii_case("rels")
        })
}

fn validate_xlsx(package: &Package<'_>) -> Result<usize, FileError> {
    require_root(
        package,
        "xl/workbook.xml",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
    )?;
    let workbook = text_part(package, "xl/workbook.xml")?;
    let relationships = text_part(package, "xl/_rels/workbook.xml.rels")?;
    let mut sheets = 0usize;
    loop {
        let number = sheets.saturating_add(1);
        let name = format!("xl/worksheets/sheet{number}.xml");
        let Some(worksheet) = package.find(&name) else {
            break;
        };
        let worksheet =
            std::str::from_utf8(worksheet).map_err(|_| package_error(PackageIssue::InvalidXml))?;
        if !worksheet.contains("<worksheet ")
            || !worksheet.contains("<sheetData>")
            || worksheet.contains("<f")
        {
            return Err(package_error(PackageIssue::ForbiddenContent));
        }
        let target = format!("Target=\"worksheets/sheet{number}.xml\"");
        if !relationships.contains(&target) {
            return Err(package_error(PackageIssue::MissingStructure));
        }
        sheets = number;
    }
    if sheets == 0 || count(workbook, "<sheet name=") != sheets {
        return Err(package_error(PackageIssue::MissingStructure));
    }
    let mut allowed = base_parts("xl/workbook.xml");
    allowed.insert("xl/_rels/workbook.xml.rels".to_owned());
    for number in 1..=sheets {
        allowed.insert(format!("xl/worksheets/sheet{number}.xml"));
    }
    exact_paths(package, &allowed)?;
    Ok(sheets)
}

fn validate_docx(package: &Package<'_>) -> Result<usize, FileError> {
    require_root(
        package,
        "word/document.xml",
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml",
    )?;
    let document = text_part(package, "word/document.xml")?;
    if !document.contains("<w:document ")
        || !document.contains("<w:body>")
        || !document.contains("<w:sectPr>")
    {
        return Err(package_error(PackageIssue::MissingStructure));
    }
    exact_paths(package, &base_parts("word/document.xml"))?;
    Ok(1)
}

fn validate_pptx(package: &Package<'_>) -> Result<usize, FileError> {
    require_root(
        package,
        "ppt/presentation.xml",
        "application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml",
    )?;
    for required in [
        "ppt/_rels/presentation.xml.rels",
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels",
        "ppt/slideLayouts/slideLayout1.xml",
        "ppt/slideMasters/_rels/slideMaster1.xml.rels",
        "ppt/slideMasters/slideMaster1.xml",
        "ppt/theme/theme1.xml",
    ] {
        require(package, required)?;
    }
    let presentation = text_part(package, "ppt/presentation.xml")?;
    let relationships = text_part(package, "ppt/_rels/presentation.xml.rels")?;
    let mut slides = 0usize;
    loop {
        let number = slides.saturating_add(1);
        let slide_name = format!("ppt/slides/slide{number}.xml");
        let Some(slide) = package.find(&slide_name) else {
            break;
        };
        let slide =
            std::str::from_utf8(slide).map_err(|_| package_error(PackageIssue::InvalidXml))?;
        if !slide.contains("<p:sld ") || !slide.contains("<p:spTree>") {
            return Err(package_error(PackageIssue::MissingStructure));
        }
        let rel_name = format!("ppt/slides/_rels/slide{number}.xml.rels");
        let slide_rels = text_part(package, &rel_name)?;
        if !slide_rels.contains("Target=\"../slideLayouts/slideLayout1.xml\"") {
            return Err(package_error(PackageIssue::MissingStructure));
        }
        let target = format!("Target=\"slides/slide{number}.xml\"");
        if !relationships.contains(&target) {
            return Err(package_error(PackageIssue::MissingStructure));
        }
        slides = number;
    }
    if slides == 0 || count(presentation, "<p:sldId id=") != slides {
        return Err(package_error(PackageIssue::MissingStructure));
    }
    let mut allowed = base_parts("ppt/presentation.xml");
    for path in [
        "ppt/_rels/presentation.xml.rels",
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels",
        "ppt/slideLayouts/slideLayout1.xml",
        "ppt/slideMasters/_rels/slideMaster1.xml.rels",
        "ppt/slideMasters/slideMaster1.xml",
        "ppt/theme/theme1.xml",
    ] {
        allowed.insert(path.to_owned());
    }
    for number in 1..=slides {
        allowed.insert(format!("ppt/slides/slide{number}.xml"));
        allowed.insert(format!("ppt/slides/_rels/slide{number}.xml.rels"));
    }
    exact_paths(package, &allowed)?;
    Ok(slides)
}

fn require_root(package: &Package<'_>, target: &str, content_type: &str) -> Result<(), FileError> {
    let root = text_part(package, "_rels/.rels")?;
    let types = text_part(package, "[Content_Types].xml")?;
    let target_token = format!("Target=\"{target}\"");
    if !root.contains(&target_token)
        || !root.contains("relationships/officeDocument\"")
        || !types.contains(content_type)
    {
        return Err(package_error(PackageIssue::MissingStructure));
    }
    require(package, "docProps/core.xml")
}

fn base_parts(main: &str) -> BTreeSet<String> {
    [
        "[Content_Types].xml",
        "_rels/.rels",
        "docProps/core.xml",
        main,
    ]
    .into_iter()
    .map(str::to_owned)
    .collect()
}

fn exact_paths(package: &Package<'_>, allowed: &BTreeSet<String>) -> Result<(), FileError> {
    if package.parts.len() != allowed.len()
        || package
            .parts
            .iter()
            .any(|part| !allowed.contains(part.name))
    {
        return Err(package_error(PackageIssue::ForbiddenContent));
    }
    Ok(())
}

fn require(package: &Package<'_>, name: &str) -> Result<(), FileError> {
    package
        .find(name)
        .map(|_| ())
        .ok_or_else(|| package_error(PackageIssue::MissingStructure))
}

fn text_part<'a>(package: &'a Package<'a>, name: &str) -> Result<&'a str, FileError> {
    let bytes = package
        .find(name)
        .ok_or_else(|| package_error(PackageIssue::MissingStructure))?;
    std::str::from_utf8(bytes).map_err(|_| package_error(PackageIssue::InvalidXml))
}

fn reject_active_content(bytes: &[u8]) -> Result<(), FileError> {
    const FORBIDDEN: [&str; 8] = [
        "TargetMode=\"External\"",
        "vbaProject",
        "externalLink",
        "oleObject",
        "embeddedPackage",
        "javascript",
        "<f>",
        "<f ",
    ];
    let text = std::str::from_utf8(bytes).map_err(|_| package_error(PackageIssue::InvalidXml))?;
    if FORBIDDEN.into_iter().any(|token| text.contains(token)) {
        return Err(package_error(PackageIssue::ForbiddenContent));
    }
    Ok(())
}

fn count(text: &str, token: &str) -> usize {
    text.match_indices(token).count()
}

fn package_error(issue: PackageIssue) -> FileError {
    FileError::InvalidPackage(issue)
}

#[cfg(test)]
mod tests;
