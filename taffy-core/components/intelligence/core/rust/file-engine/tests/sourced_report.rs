// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use file_engine::{
    generate_sourced_report, FindingBasis, Format, Limits, ReportFinding, ReportSource,
    SourcedReport,
};

fn report() -> SourcedReport {
    SourcedReport {
        title: "Phone comparison".to_owned(),
        sources: vec![
            ReportSource {
                key: "source-b".to_owned(),
                title: "Beta specifications".to_owned(),
                display_locator: "beta.example".to_owned(),
            },
            ReportSource {
                key: "source-a".to_owned(),
                title: "Alpha specifications".to_owned(),
                display_locator: "alpha.example".to_owned(),
            },
        ],
        findings: vec![
            ReportFinding {
                subject: "Beta".to_owned(),
                field: "Battery".to_owned(),
                value: "18 hours".to_owned(),
                basis: FindingBasis::Summary,
                source_keys: vec!["source-b".to_owned()],
            },
            ReportFinding {
                subject: "Alpha".to_owned(),
                field: "Price".to_owned(),
                value: "=499+tax".to_owned(),
                basis: FindingBasis::Page,
                source_keys: vec!["source-a".to_owned()],
            },
            ReportFinding {
                subject: "Choice".to_owned(),
                field: "Colour".to_owned(),
                value: "Blue".to_owned(),
                basis: FindingBasis::User,
                source_keys: Vec::new(),
            },
        ],
    }
}

#[test]
fn every_sourced_format_is_deterministic_and_structurally_validated() {
    for format in [Format::Xlsx, Format::Docx, Format::Pptx, Format::Pdf] {
        let Ok(first) = generate_sourced_report(format, &report(), Limits::default()) else {
            unreachable!("sample sourced report generates");
        };
        let mut shuffled = report();
        shuffled.sources.reverse();
        shuffled.findings.reverse();
        let Ok(second) = generate_sourced_report(format, &shuffled, Limits::default()) else {
            unreachable!("shuffled sourced report generates");
        };
        assert_eq!(first.bytes(), second.bytes(), "{format:?}");
        assert_eq!(first.format(), format);
        assert!(first.validation().structural_parts > 0);
        assert!(first.validation().logical_units > 0);
    }
}

#[test]
fn the_caller_can_lower_the_output_ceiling_without_a_partial_file() {
    let limits = Limits {
        max_output_bytes: 128,
        ..Limits::default()
    };
    assert!(generate_sourced_report(Format::Xlsx, &report(), limits).is_err());
}
