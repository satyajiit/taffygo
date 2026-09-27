// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use file_engine::{
    generate, validate, Block, Cell, Deck, Document, FileSpec, Format, Limits, PdfDocument,
    PdfPage, Sheet, Slide, Workbook,
};

fn workbook() -> FileSpec {
    FileSpec::Workbook(Workbook {
        title: "Quarterly <comparison> & notes".to_owned(),
        sheets: vec![Sheet {
            name: "Summary".to_owned(),
            rows: vec![
                vec![
                    Cell::Text("Item".to_owned()),
                    Cell::Text("Value".to_owned()),
                    Cell::Text("Literal".to_owned()),
                ],
                vec![
                    Cell::Text("Alpha".to_owned()),
                    Cell::Number(42.5),
                    Cell::Text("=1+1".to_owned()),
                ],
                vec![
                    Cell::Text("Beta".to_owned()),
                    Cell::Boolean(true),
                    Cell::Blank,
                ],
            ],
        }],
    })
}

fn document() -> FileSpec {
    FileSpec::Document(Document {
        title: "Research brief".to_owned(),
        blocks: vec![
            Block::Heading {
                level: 1,
                text: "Findings & limits".to_owned(),
            },
            Block::Paragraph("A literal <tag> remains text.".to_owned()),
            Block::Table(vec![
                vec!["Source".to_owned(), "Claim".to_owned()],
                vec!["Page A".to_owned(), "Verified".to_owned()],
            ]),
        ],
    })
}

fn deck() -> FileSpec {
    FileSpec::Deck(Deck {
        title: "Comparison".to_owned(),
        slides: vec![
            Slide {
                title: "Overview".to_owned(),
                body: vec!["One & two".to_owned(), "Three < four".to_owned()],
            },
            Slide {
                title: "Decision".to_owned(),
                body: vec!["Choose the bounded path.".to_owned()],
            },
        ],
    })
}

fn pdf() -> FileSpec {
    FileSpec::Pdf(PdfDocument {
        title: "Report (final)".to_owned(),
        pages: vec![
            PdfPage {
                lines: vec!["First page".to_owned(), "A (literal) \\ value".to_owned()],
            },
            PdfPage {
                lines: vec!["Second page".to_owned()],
            },
        ],
    })
}

#[test]
fn every_format_generates_deterministically_and_round_trips_through_validation() {
    for spec in [workbook(), document(), deck(), pdf()] {
        let Ok(first) = generate(&spec, Limits::default()) else {
            unreachable!("sample generates");
        };
        let Ok(second) = generate(&spec, Limits::default()) else {
            unreachable!("same sample generates again");
        };
        assert_eq!(first.bytes(), second.bytes());
        assert_eq!(first.format(), spec.format());
        assert_eq!(first.extension(), spec.format().extension());
        assert_eq!(first.mime_type(), spec.format().mime_type());
        assert_eq!(
            validate(first.format(), first.bytes(), Limits::default()),
            Ok(first.validation())
        );
    }
}

#[test]
fn logical_unit_counts_are_closed_per_format() {
    let cases = [
        (workbook(), Format::Xlsx, 1),
        (document(), Format::Docx, 1),
        (deck(), Format::Pptx, 2),
        (pdf(), Format::Pdf, 2),
    ];
    for (spec, format, units) in cases {
        let Ok(file) = generate(&spec, Limits::default()) else {
            unreachable!("sample generates");
        };
        assert_eq!(file.validation().format, format);
        assert_eq!(file.validation().logical_units, units);
        assert_eq!(file.validation().byte_len, file.bytes().len());
        assert!(file.validation().structural_parts > 0);
    }
}
