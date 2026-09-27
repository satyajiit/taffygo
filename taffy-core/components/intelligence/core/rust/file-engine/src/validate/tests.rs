// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::validate;
use crate::zip::{self, Entry, Package};
use crate::{generate, Cell, FileError, FileSpec, Format, Limits, PackageIssue, Sheet, Workbook};

#[test]
fn external_relationship_formula_and_unknown_part_are_refused_with_valid_crcs() {
    let limits = Limits::default();
    let original = generated_xlsx();

    let external = rewrite(&original, limits, "_rels/.rels", |xml| {
        xml.replace(
            "Target=\"xl/workbook.xml\"",
            "Target=\"xl/workbook.xml\" TargetMode=\"External\"",
        )
    });
    assert!(external
        .windows(b"TargetMode=\"External\"".len())
        .any(|window| window == b"TargetMode=\"External\""));
    assert_eq!(
        validate(Format::Xlsx, &external, limits),
        Err(FileError::InvalidPackage(PackageIssue::ForbiddenContent))
    );

    let formula = rewrite(&original, limits, "xl/worksheets/sheet1.xml", |xml| {
        xml.replace(
            "</sheetData>",
            "<row r=\"2\"><c r=\"A2\"><f>1+1</f></c></row></sheetData>",
        )
    });
    assert_eq!(
        validate(Format::Xlsx, &formula, limits),
        Err(FileError::InvalidPackage(PackageIssue::ForbiddenContent))
    );

    let Ok(package) = Package::parse(&original, limits) else {
        unreachable!("fixture parses");
    };
    let mut entries = owned_entries(&package);
    entries.push(Entry::new(
        "xl/unknown.xml",
        b"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><unknown/>".to_vec(),
    ));
    let Ok(unknown) = zip::write(entries, limits) else {
        unreachable!("package rewrites");
    };
    assert_eq!(
        validate(Format::Xlsx, &unknown, limits),
        Err(FileError::InvalidPackage(PackageIssue::ForbiddenContent))
    );
}

#[test]
fn a_required_part_cannot_disappear_behind_a_well_formed_archive() {
    let limits = Limits::default();
    let original = generated_xlsx();
    let Ok(package) = Package::parse(&original, limits) else {
        unreachable!("fixture parses");
    };
    let entries: Vec<Entry> = package
        .parts
        .iter()
        .filter(|part| part.name != "xl/workbook.xml")
        .map(|part| Entry::new(part.name, part.data.to_vec()))
        .collect();
    let Ok(missing) = zip::write(entries, limits) else {
        unreachable!("package rewrites");
    };
    assert_eq!(
        validate(Format::Xlsx, &missing, limits),
        Err(FileError::InvalidPackage(PackageIssue::MissingStructure))
    );
}

fn generated_xlsx() -> Vec<u8> {
    let Ok(file) = generate(
        &FileSpec::Workbook(Workbook {
            title: "Workbook".to_owned(),
            sheets: vec![Sheet {
                name: "Data".to_owned(),
                rows: vec![vec![Cell::Text("literal".to_owned())]],
            }],
        }),
        Limits::default(),
    ) else {
        unreachable!("fixture generates");
    };
    file.into_bytes()
}

fn rewrite(
    bytes: &[u8],
    limits: Limits,
    target: &str,
    change: impl FnOnce(&str) -> String,
) -> Vec<u8> {
    let Ok(package) = Package::parse(bytes, limits) else {
        unreachable!("fixture parses");
    };
    let mut change = Some(change);
    let entries = package
        .parts
        .iter()
        .map(|part| {
            if part.name == target {
                let Ok(xml) = std::str::from_utf8(part.data) else {
                    unreachable!("fixture XML is UTF-8");
                };
                let Some(apply) = change.take() else {
                    unreachable!("target appears once");
                };
                Entry::new(part.name, apply(xml).into_bytes())
            } else {
                Entry::new(part.name, part.data.to_vec())
            }
        })
        .collect();
    let Ok(rewritten) = zip::write(entries, limits) else {
        unreachable!("package rewrites");
    };
    rewritten
}

fn owned_entries(package: &Package<'_>) -> Vec<Entry> {
    package
        .parts
        .iter()
        .map(|part| Entry::new(part.name, part.data.to_vec()))
        .collect()
}
