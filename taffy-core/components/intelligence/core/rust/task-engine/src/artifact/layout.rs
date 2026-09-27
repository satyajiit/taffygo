// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Ordering the material an artifact renders, before any format sees it.
//!
//! This is where "byte-identical for identical input" is established. Sources
//! are numbered in a total order, facts are sorted into one, and every fact is
//! matched to the evidence that cites it. A fact that was derived externally
//! and carries no locator is refused here, so neither renderer has to decide
//! what to do about one.
//!
//! Both renderers consume the result and neither may reorder it.

use std::collections::BTreeMap;
use std::fmt::Write;

use super::error::ArtifactError;
use super::request::ArtifactRequest;
use crate::records::{Fact, FactClassification, ProvenanceKind, ProvenanceLocator, Source};
/// The plain words for how a fact came to be.
pub(super) const fn classification_text(classification: FactClassification) -> &'static str {
    match classification {
        FactClassification::UserEntered => "You entered it",
        FactClassification::Extracted => "Read from the page",
        FactClassification::Summarized => "Summarized from the page",
        FactClassification::Inferred => "Worked out from other values",
    }
}

/// The plain words for what kind of evidence a locator is.
pub(super) const fn provenance_text(kind: ProvenanceKind) -> &'static str {
    match kind {
        ProvenanceKind::Dom => "Page content",
        ProvenanceKind::Accessibility => "Accessibility tree",
        ProvenanceKind::StructuredData => "Structured data",
        ProvenanceKind::User => "You",
        ProvenanceKind::Model => "Model output",
        ProvenanceKind::Tool => "Tool output",
    }
}
/// A source that has been given a citation number.
pub(super) struct NumberedSource<'a> {
    pub(super) number: usize,
    pub(super) source: &'a Source,
}

/// One row of the findings table, already ordered.
pub(super) struct Row<'a> {
    pub(super) fact: &'a Fact,
    pub(super) provenance: Vec<NumberedEvidence<'a>>,
    pub(super) citations: Vec<usize>,
}

pub(super) struct NumberedEvidence<'a> {
    pub(super) number: usize,
    pub(super) locator: &'a ProvenanceLocator,
}

pub(super) fn prepare(
    request: &ArtifactRequest,
) -> Result<(Vec<NumberedSource<'_>>, Vec<Row<'_>>), ArtifactError> {
    if request.facts.is_empty() {
        return Err(ArtifactError::NoFacts);
    }

    let mut sources: Vec<&Source> = request.sources.iter().collect();
    sources.sort_by(|left, right| {
        left.display_locator
            .cmp(&right.display_locator)
            .then_with(|| left.source_id.cmp(&right.source_id))
    });
    sources.dedup_by(|left, right| left.source_id == right.source_id);

    let mut numbers = BTreeMap::new();
    let numbered: Vec<NumberedSource<'_>> = sources
        .into_iter()
        .enumerate()
        .map(|(index, source)| {
            let number = index.saturating_add(1);
            numbers.insert(source.source_id, number);
            NumberedSource { number, source }
        })
        .collect();

    let mut rows: Vec<Row<'_>> = Vec::with_capacity(request.facts.len());
    for cited in &request.facts {
        let externally_derived = cited.fact.classification != FactClassification::UserEntered;
        if externally_derived && cited.provenance.is_empty() {
            return Err(ArtifactError::UncitedFact {
                fact_id: cited.fact.fact_id.to_text(),
            });
        }
        let mut provenance: Vec<&ProvenanceLocator> = cited.provenance.iter().collect();
        provenance.sort_by(|left, right| {
            left.source_id
                .cmp(&right.source_id)
                .then_with(|| left.location_descriptor.cmp(&right.location_descriptor))
                .then_with(|| left.provenance_id.cmp(&right.provenance_id))
        });
        let mut citations: Vec<usize> = Vec::with_capacity(provenance.len());
        let mut numbered_evidence = Vec::with_capacity(provenance.len());
        for locator in provenance {
            let number = numbers.get(&locator.source_id).copied().ok_or_else(|| {
                ArtifactError::UnknownSource {
                    source_id: locator.source_id.to_text(),
                }
            })?;
            if citations.last() != Some(&number) {
                citations.push(number);
            }
            numbered_evidence.push(NumberedEvidence { number, locator });
        }
        citations.sort_unstable();
        rows.push(Row {
            fact: &cited.fact,
            provenance: numbered_evidence,
            citations,
        });
    }

    rows.sort_by(|left, right| {
        left.fact
            .subject_key
            .cmp(&right.fact.subject_key)
            .then_with(|| left.fact.predicate.cmp(&right.fact.predicate))
            .then_with(|| left.fact.typed_value.cmp(&right.fact.typed_value))
            .then_with(|| left.fact.fact_id.cmp(&right.fact.fact_id))
    });

    Ok((numbered, rows))
}

pub(super) fn citation_text(citations: &[usize]) -> String {
    if citations.is_empty() {
        return "You".to_owned();
    }
    let mut out = String::with_capacity(citations.len().saturating_mul(4));
    for (index, number) in citations.iter().enumerate() {
        if index > 0 {
            out.push(' ');
        }
        let _ = write!(out, "[{number}]");
    }
    out
}

pub(super) fn citation_numbers(citations: &[usize]) -> String {
    let mut out = String::with_capacity(citations.len().saturating_mul(3));
    for (index, number) in citations.iter().enumerate() {
        if index > 0 {
            out.push(' ');
        }
        let _ = write!(out, "{number}");
    }
    out
}
