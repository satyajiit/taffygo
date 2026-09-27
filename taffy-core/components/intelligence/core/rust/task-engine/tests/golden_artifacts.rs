// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The milestone M3 export contract: byte-identical output, fact-level
//! citations, and hostile values that stay text.
//!
//! The golden files in `tests/golden/` are the contract. A change to them is a
//! change to what a person receives, which is exactly the kind of change that
//! should be visible in review.
//!
//! The fixture is deliberately unpleasant: one of its values is a spreadsheet
//! formula wearing a Markdown link and a bidirectional override. If any of that
//! survives into an artifact, the golden comparison says so.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::artifact::{generate, generate_within, ArtifactError, ArtifactKind, CitedFact};
use task_engine::ids::ArtifactId;
use task_engine::{ArtifactRequest, ObservationId, ProvenanceId, WorkspaceId};
use task_engine::{
    DeletionState, Fact, FactClassification, FactStatus, Ownership, ProvenanceKind,
    ProvenanceLocator, Sensitivity, Source, SourceKind, Timestamp,
};

fn stamp(text: &str) -> Timestamp {
    match Timestamp::new(text) {
        Ok(stamp) => stamp,
        Err(_) => unreachable!("the fixture timestamps are well formed"),
    }
}

fn provenance_id(seed: u8) -> ProvenanceId {
    let mut bytes = [0_u8; 16];
    bytes[15] = seed;
    ProvenanceId::from_bytes(bytes)
}

fn observation_id(seed: u8) -> ObservationId {
    let mut bytes = [0_u8; 16];
    bytes[15] = seed;
    ObservationId::from_bytes(bytes)
}

fn source(seed: u8, locator: &str, title: &str) -> Source {
    Source {
        source_id: common::source_id(seed),
        kind: SourceKind::WebPage,
        canonical_locator: None,
        display_locator: locator.to_owned(),
        origin: Some("https://example.test".to_owned()),
        title: Some(title.to_owned()),
        first_seen_at: stamp("2026-08-17T09:00:00Z"),
        last_observed_at: Some(stamp("2026-08-17T09:05:00Z")),
        ownership: Ownership::External,
        sensitivity: Sensitivity::Public,
        retention_class: "workspace".to_owned(),
        deletion_state: DeletionState::Active,
    }
}

fn fact(
    seed: u8,
    subject: &str,
    predicate: &str,
    value: &str,
    unit: Option<&str>,
    classification: FactClassification,
) -> Fact {
    Fact {
        fact_id: common::fact_id(seed),
        workspace_id: {
            let mut bytes = [0_u8; 16];
            bytes[15] = 9;
            WorkspaceId::from_bytes(bytes)
        },
        subject_key: subject.to_owned(),
        predicate: predicate.to_owned(),
        typed_value: value.to_owned(),
        unit: unit.map(str::to_owned),
        classification,
        confidence_basis_points: Some(9_000),
        observation_time: stamp("2026-08-17T09:05:00Z"),
        sensitivity: Sensitivity::Public,
        status: FactStatus::Accepted,
        supersedes_fact_id: None,
        retention_class: "workspace".to_owned(),
    }
}

fn locator(
    seed: u8,
    source_seed: u8,
    where_in_source: &str,
    kind: ProvenanceKind,
) -> ProvenanceLocator {
    ProvenanceLocator {
        provenance_id: provenance_id(seed),
        fact_id: common::fact_id(seed),
        source_id: common::source_id(source_seed),
        observation_id: Some(observation_id(source_seed)),
        kind,
        location_descriptor: Some(where_in_source.to_owned()),
        extraction_rule_version: Some("rule_1".to_owned()),
        transformation_chain: "normalize".to_owned(),
        captured_at: stamp("2026-08-17T09:05:00Z"),
    }
}

/// The fixture, with its rows deliberately out of order.
fn request() -> ArtifactRequest {
    ArtifactRequest {
        artifact_id: ArtifactId::new("artifact_0"),
        title: "Compare two laptops".to_owned(),
        schema_version: 1,
        sources: vec![
            source(2, "example.test/model-b", "Model B specification"),
            source(1, "example.test/model-a", "Model A specification"),
        ],
        facts: vec![
            CitedFact {
                fact: fact(
                    3,
                    "Model B",
                    "battery life",
                    "=SUM(A1:A9)",
                    Some("hours"),
                    FactClassification::Extracted,
                ),
                provenance: vec![locator(
                    3,
                    2,
                    "Specifications table, row 4",
                    ProvenanceKind::Dom,
                )],
            },
            CitedFact {
                fact: fact(
                    1,
                    "Model A",
                    "price",
                    "1299",
                    Some("USD"),
                    FactClassification::Extracted,
                ),
                provenance: vec![
                    locator(1, 1, "Pricing panel", ProvenanceKind::StructuredData),
                    locator(1, 2, "Comparison table, column 1", ProvenanceKind::Dom),
                ],
            },
            CitedFact {
                fact: fact(
                    2,
                    "Model A",
                    "summary",
                    "[Best value](javascript:alert(1)) | fast\u{202e}gnp.exe",
                    None,
                    FactClassification::Summarized,
                ),
                provenance: vec![locator(2, 1, "Overview paragraph", ProvenanceKind::Model)],
            },
            CitedFact {
                fact: fact(
                    4,
                    "Model A",
                    "note",
                    "I already own the charger",
                    None,
                    FactClassification::UserEntered,
                ),
                provenance: Vec::new(),
            },
        ],
    }
}

/// The same request with everything shuffled.
fn shuffled() -> ArtifactRequest {
    let mut request = request();
    request.sources.reverse();
    request.facts.reverse();
    for cited in &mut request.facts {
        cited.provenance.reverse();
    }
    request
}

fn golden(name: &str) -> String {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests")
        .join("golden")
        .join(name);
    match std::fs::read_to_string(&path) {
        Ok(text) => text,
        Err(error) => unreachable!("the golden {name} must exist: {error}"),
    }
}

#[test]
fn the_markdown_export_matches_its_golden() {
    let artifact = match generate(&request(), ArtifactKind::Markdown) {
        Ok(artifact) => artifact,
        Err(error) => unreachable!("the fixture generates: {error:?}"),
    };
    assert_eq!(artifact.content, golden("research.md"));
    assert_eq!(artifact.fact_count, 4);
    assert_eq!(artifact.source_count, 2);
}

#[test]
fn the_comma_separated_export_matches_its_golden() {
    let artifact = match generate(&request(), ArtifactKind::Csv) {
        Ok(artifact) => artifact,
        Err(error) => unreachable!("the fixture generates: {error:?}"),
    };
    assert_eq!(artifact.content, golden("research.csv"));
}

#[test]
fn the_same_records_produce_the_same_bytes_however_they_arrived() {
    for kind in ArtifactKind::TEXT {
        let (Ok(first), Ok(second), Ok(third)) = (
            generate(&request(), *kind),
            generate(&request(), *kind),
            generate(&shuffled(), *kind),
        ) else {
            unreachable!("the fixture generates in {}", kind.label())
        };
        assert_eq!(
            first.content,
            second.content,
            "{} is not repeatable",
            kind.label()
        );
        assert_eq!(
            first.content,
            third.content,
            "{} depends on the order the caller collected its rows",
            kind.label()
        );
        assert_eq!(first.checksum, third.checksum);
    }
}

#[test]
fn rich_formats_fail_closed_in_the_text_renderer() {
    for kind in ArtifactKind::RICH {
        assert_eq!(
            generate(&request(), *kind),
            Err(ArtifactError::RichFormatRequiresFileEngine)
        );
    }
}

#[test]
fn a_hostile_value_reaches_neither_export_as_markup_or_as_a_formula() {
    let (Ok(markdown), Ok(csv)) = (
        generate(&request(), ArtifactKind::Markdown),
        generate(&request(), ArtifactKind::Csv),
    ) else {
        unreachable!("the fixture generates in both formats")
    };

    for content in [&markdown.content, &csv.content] {
        assert!(
            !content.contains('\u{202e}'),
            "a bidirectional override survived"
        );
        assert!(
            !content.contains('\n') || !content.contains('\r'),
            "a value kept a line break"
        );
    }

    // Markdown: the link syntax is escaped, so it renders as the text it is.
    assert!(!markdown.content.contains("](javascript:"));
    assert!(markdown
        .content
        .contains("\\[Best value\\]\\(javascript:alert\\(1\\)\\)"));
    assert!(markdown.content.contains("fast gnp.exe"));

    // Comma separated: every cell is quoted and the formula is inert.
    assert!(csv.content.contains("\"\'=SUM(A1:A9)\""));
    for line in csv.content.lines().skip(1) {
        assert!(
            line.starts_with('"') && line.ends_with('"'),
            "unquoted row: {line}"
        );
    }
}

#[test]
fn an_externally_derived_fact_without_evidence_is_refused() {
    let mut request = request();
    if let Some(cited) = request.facts.first_mut() {
        cited.provenance.clear();
    }
    let error = generate(&request, ArtifactKind::Markdown)
        .err()
        .unwrap_or_else(|| unreachable!("an uncited extracted fact is refused"));
    assert_eq!(error.label(), "uncited_fact");
}

#[test]
fn a_locator_citing_a_source_outside_the_request_is_refused() {
    let mut request = request();
    request
        .sources
        .retain(|source| source.source_id == common::source_id(1));
    let error = generate(&request, ArtifactKind::Markdown)
        .err()
        .unwrap_or_else(|| unreachable!("an unknown source is refused"));
    assert_eq!(error.label(), "unknown_source");
}

#[test]
fn an_export_with_nothing_in_it_is_refused() {
    let mut request = request();
    request.facts.clear();
    assert_eq!(
        generate(&request, ArtifactKind::Csv).err(),
        Some(ArtifactError::NoFacts)
    );
}

#[test]
fn an_artifact_larger_than_its_budget_is_refused() {
    let request = request();
    let Ok(artifact) = generate(&request, ArtifactKind::Markdown) else {
        unreachable!("the fixture generates")
    };
    let limit = artifact.byte_len().saturating_sub(1);
    let error = generate_within(&request, ArtifactKind::Markdown, limit)
        .err()
        .unwrap_or_else(|| unreachable!("an oversized artifact is refused"));
    assert_eq!(error.label(), "too_large");
    assert!(generate_within(&request, ArtifactKind::Markdown, artifact.byte_len()).is_ok());
}

#[test]
fn every_fact_carries_its_citations_and_every_source_is_numbered() {
    let Ok(markdown) = generate(&request(), ArtifactKind::Markdown) else {
        unreachable!("the fixture generates")
    };
    // Two sources, numbered one and two, and both cited.
    assert!(markdown.content.contains("1. `example.test/model-a`"));
    assert!(markdown.content.contains("2. `example.test/model-b`"));
    assert!(markdown.content.contains("| [1] [2] |"));
    // The user-entered fact says so rather than citing a page.
    assert!(markdown.content.contains("You entered it"));
}
