// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use file_engine::{
    generate, validate, Block, Cell, Deck, Document, FileError, FileSpec, Format, InputIssue,
    LimitKind, Limits, PdfDocument, PdfPage, Sheet, Slide, Workbook,
};

fn one_sheet(rows: Vec<Vec<Cell>>) -> FileSpec {
    FileSpec::Workbook(Workbook {
        title: "Workbook".to_owned(),
        sheets: vec![Sheet {
            name: "Data".to_owned(),
            rows,
        }],
    })
}

#[test]
fn formula_sigils_are_always_literal_inline_strings() {
    let spec = one_sheet(vec![vec![
        Cell::Text("=2+2".to_owned()),
        Cell::Text("+cmd".to_owned()),
        Cell::Text("-1+1".to_owned()),
        Cell::Text("@SUM(A1:A2)".to_owned()),
    ]]);
    let Ok(file) = generate(&spec, Limits::default()) else {
        unreachable!("literal cells generate");
    };
    assert!(contains(file.bytes(), b"t=\"inlineStr\""));
    assert!(contains(file.bytes(), b"=2+2"));
    assert!(!contains(file.bytes(), b"<f"));
}

#[test]
fn xml_metacharacters_are_escaped_in_every_ooxml_surface() {
    let specs = [
        one_sheet(vec![vec![Cell::Text("<&>".to_owned())]]),
        FileSpec::Document(Document {
            title: "<&>".to_owned(),
            blocks: vec![Block::Paragraph("<&>".to_owned())],
        }),
        FileSpec::Deck(Deck {
            title: "<&>".to_owned(),
            slides: vec![Slide {
                title: "<&>".to_owned(),
                body: vec!["<&>".to_owned()],
            }],
        }),
    ];
    for spec in specs {
        let Ok(file) = generate(&spec, Limits::default()) else {
            unreachable!("escaped content generates");
        };
        assert!(contains(file.bytes(), b"&lt;&amp;&gt;"));
        assert!(!contains(file.bytes(), b"><&><"));
    }
}

#[test]
fn malformed_input_is_refused_before_file_delivery() {
    let invalid_names = [
        "",
        "bad/name",
        "bad:name",
        "'reserved'",
        "a1234567890123456789012345678901",
    ];
    for name in invalid_names {
        let spec = FileSpec::Workbook(Workbook {
            title: "Workbook".to_owned(),
            sheets: vec![Sheet {
                name: name.to_owned(),
                rows: vec![],
            }],
        });
        assert_eq!(
            generate(&spec, Limits::default()),
            Err(FileError::InvalidInput(InputIssue::InvalidSheetName))
        );
    }
    let duplicate = FileSpec::Workbook(Workbook {
        title: "Workbook".to_owned(),
        sheets: vec![
            Sheet {
                name: "Data".to_owned(),
                rows: vec![],
            },
            Sheet {
                name: "DATA".to_owned(),
                rows: vec![],
            },
        ],
    });
    assert_eq!(
        generate(&duplicate, Limits::default()),
        Err(FileError::InvalidInput(InputIssue::InvalidSheetName))
    );
    assert_eq!(
        generate(
            &one_sheet(vec![vec![Cell::Number(f64::NAN)]]),
            Limits::default()
        ),
        Err(FileError::InvalidInput(InputIssue::NonFiniteNumber))
    );
    let bad_heading = FileSpec::Document(Document {
        title: "Document".to_owned(),
        blocks: vec![Block::Heading {
            level: 0,
            text: "No level".to_owned(),
        }],
    });
    assert_eq!(
        generate(&bad_heading, Limits::default()),
        Err(FileError::InvalidInput(InputIssue::InvalidHeadingLevel))
    );
}

#[test]
fn control_characters_and_unrenderable_pdf_text_are_refused() {
    let xml_control = FileSpec::Document(Document {
        title: "Document".to_owned(),
        blocks: vec![Block::Paragraph("bad\u{0}text".to_owned())],
    });
    assert_eq!(
        generate(&xml_control, Limits::default()),
        Err(FileError::InvalidInput(InputIssue::UnsupportedCharacter))
    );
    let unicode_pdf = FileSpec::Pdf(PdfDocument {
        title: "Résumé".to_owned(),
        pages: vec![PdfPage { lines: vec![] }],
    });
    assert_eq!(
        generate(&unicode_pdf, Limits::default()),
        Err(FileError::InvalidInput(InputIssue::UnsupportedCharacter))
    );
}

#[test]
fn byte_text_and_spreadsheet_ceilings_are_enforced_by_name() {
    let base = Limits::default();
    assert_limit(
        &one_sheet(vec![]),
        Limits {
            max_output_bytes: 100,
            ..base
        },
        LimitKind::OutputBytes,
        100,
    );
    assert_limit(
        &one_sheet(vec![vec![Cell::Text("long".to_owned())]]),
        Limits {
            max_text_bytes: 1,
            ..base
        },
        LimitKind::TextBytes,
        1,
    );
    let two_sheets = FileSpec::Workbook(Workbook {
        title: "Workbook".to_owned(),
        sheets: vec![
            Sheet {
                name: "A".to_owned(),
                rows: vec![],
            },
            Sheet {
                name: "B".to_owned(),
                rows: vec![],
            },
        ],
    });
    assert_limit(
        &two_sheets,
        Limits {
            max_sheets: 1,
            ..base
        },
        LimitKind::Sheets,
        1,
    );
    assert_limit(
        &one_sheet(vec![vec![], vec![]]),
        Limits {
            max_rows: 1,
            ..base
        },
        LimitKind::Rows,
        1,
    );
    assert_limit(
        &one_sheet(vec![vec![Cell::Blank, Cell::Blank]]),
        Limits {
            max_columns: 1,
            ..base
        },
        LimitKind::Columns,
        1,
    );
    assert_limit(
        &one_sheet(vec![vec![Cell::Boolean(true), Cell::Boolean(false)]]),
        Limits {
            max_cells: 1,
            ..base
        },
        LimitKind::Cells,
        1,
    );
}

#[test]
fn document_presentation_and_pdf_ceilings_are_enforced_by_name() {
    let base = Limits::default();
    let two_blocks = FileSpec::Document(Document {
        title: "Document".to_owned(),
        blocks: vec![
            Block::Paragraph("a".to_owned()),
            Block::Paragraph("b".to_owned()),
        ],
    });
    assert_limit(
        &two_blocks,
        Limits {
            max_blocks: 1,
            ..base
        },
        LimitKind::Blocks,
        1,
    );
    let two_slides = FileSpec::Deck(Deck {
        title: "Deck".to_owned(),
        slides: vec![
            Slide {
                title: "A".to_owned(),
                body: vec![],
            },
            Slide {
                title: "B".to_owned(),
                body: vec![],
            },
        ],
    });
    assert_limit(
        &two_slides,
        Limits {
            max_slides: 1,
            ..base
        },
        LimitKind::Slides,
        1,
    );
    let two_pages = FileSpec::Pdf(PdfDocument {
        title: "PDF".to_owned(),
        pages: vec![PdfPage { lines: vec![] }, PdfPage { lines: vec![] }],
    });
    assert_limit(
        &two_pages,
        Limits {
            max_pages: 1,
            ..base
        },
        LimitKind::Pages,
        1,
    );
    let two_lines = FileSpec::Pdf(PdfDocument {
        title: "PDF".to_owned(),
        pages: vec![PdfPage {
            lines: vec!["a".to_owned(), "b".to_owned()],
        }],
    });
    assert_limit(
        &two_lines,
        Limits {
            max_lines_per_page: 1,
            ..base
        },
        LimitKind::Lines,
        1,
    );
}

#[test]
fn package_and_limit_configuration_ceilings_are_enforced_by_name() {
    let base = Limits::default();
    assert_limit(
        &one_sheet(vec![]),
        Limits {
            max_package_parts: 5,
            ..base
        },
        LimitKind::PackageParts,
        5,
    );
    assert_eq!(
        generate(
            &one_sheet(vec![]),
            Limits {
                max_rows: 0,
                ..base
            }
        ),
        Err(FileError::InvalidInput(InputIssue::InvalidLimits))
    );
}

#[test]
fn damaged_or_mislabeled_bytes_never_validate() {
    let Ok(file) = generate(
        &one_sheet(vec![vec![Cell::Text("safe".to_owned())]]),
        Limits::default(),
    ) else {
        unreachable!("fixture generates");
    };
    assert!(validate(Format::Docx, file.bytes(), Limits::default()).is_err());

    let mut truncated = file.bytes().to_vec();
    truncated.pop();
    assert!(validate(Format::Xlsx, &truncated, Limits::default()).is_err());

    let mut corrupted = file.bytes().to_vec();
    let Some(payload) = corrupted.windows(4).position(|window| window == b"safe") else {
        unreachable!("stored payload exists");
    };
    let Some(byte) = corrupted.get_mut(payload) else {
        unreachable!("payload byte exists");
    };
    *byte ^= 1;
    assert!(validate(Format::Xlsx, &corrupted, Limits::default()).is_err());

    let Ok(pdf) = generate(
        &FileSpec::Pdf(PdfDocument {
            title: "PDF".to_owned(),
            pages: vec![PdfPage {
                lines: vec!["safe".to_owned()],
            }],
        }),
        Limits::default(),
    ) else {
        unreachable!("PDF fixture generates");
    };
    let mut appended = pdf.into_bytes();
    appended.extend_from_slice(b"garbage");
    assert!(validate(Format::Pdf, &appended, Limits::default()).is_err());
}

fn assert_limit(spec: &FileSpec, limits: Limits, kind: LimitKind, maximum: usize) {
    assert_eq!(
        generate(spec, limits),
        Err(FileError::LimitExceeded { kind, maximum })
    );
}

fn contains(haystack: &[u8], needle: &[u8]) -> bool {
    haystack
        .windows(needle.len())
        .any(|window| window == needle)
}
