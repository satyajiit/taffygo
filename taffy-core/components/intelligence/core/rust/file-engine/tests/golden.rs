// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use file_engine::{
    generate, Block, Cell, Deck, Document, FileSpec, Limits, PdfDocument, PdfPage, Sheet, Slide,
    Workbook,
};

#[test]
fn canonical_files_match_reviewed_byte_fingerprints() {
    let actual: Vec<(&str, usize, u32)> = specimens()
        .into_iter()
        .map(|(name, spec)| {
            let Ok(file) = generate(&spec, Limits::default()) else {
                unreachable!("golden specimen generates");
            };
            (name, file.bytes().len(), crc32(file.bytes()))
        })
        .collect();
    assert_eq!(
        actual,
        vec![
            ("xlsx", 3_532, 0xaf77_127e),
            ("docx", 2_827, 0xcd3f_0d03),
            ("pptx", 10_207, 0x1436_6227),
            ("pdf", 820, 0xc419_0fc4),
        ]
    );
}

fn specimens() -> Vec<(&'static str, FileSpec)> {
    vec![
        (
            "xlsx",
            FileSpec::Workbook(Workbook {
                title: "Golden workbook".to_owned(),
                sheets: vec![Sheet {
                    name: "Data".to_owned(),
                    rows: vec![
                        vec![
                            Cell::Text("Name".to_owned()),
                            Cell::Text("Value".to_owned()),
                        ],
                        vec![Cell::Text("Alpha".to_owned()), Cell::Number(42.5)],
                        vec![
                            Cell::Text("Literal".to_owned()),
                            Cell::Text("=1+1".to_owned()),
                        ],
                    ],
                }],
            }),
        ),
        (
            "docx",
            FileSpec::Document(Document {
                title: "Golden document".to_owned(),
                blocks: vec![
                    Block::Heading {
                        level: 1,
                        text: "Summary".to_owned(),
                    },
                    Block::Paragraph("A bounded deterministic document.".to_owned()),
                    Block::Table(vec![vec!["Source".to_owned(), "Status".to_owned()]]),
                ],
            }),
        ),
        (
            "pptx",
            FileSpec::Deck(Deck {
                title: "Golden deck".to_owned(),
                slides: vec![Slide {
                    title: "Summary".to_owned(),
                    body: vec!["One".to_owned(), "Two".to_owned()],
                }],
            }),
        ),
        (
            "pdf",
            FileSpec::Pdf(PdfDocument {
                title: "Golden PDF".to_owned(),
                pages: vec![PdfPage {
                    lines: vec!["A (literal) line".to_owned(), "A second line".to_owned()],
                }],
            }),
        ),
    ]
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            let mask = 0u32.wrapping_sub(crc & 1);
            crc = (crc >> 1) ^ (0xedb8_8320 & mask);
        }
    }
    !crc
}
