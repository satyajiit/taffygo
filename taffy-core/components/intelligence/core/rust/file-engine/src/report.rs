// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Source-bearing research material projected into every rich file format.
//!
//! This is the authoring seam above the four encoders. Callers provide facts
//! and their stable source identities; this module owns ordering, citation
//! numbering, format layout, formula-safe cells, and a source index. The same
//! report therefore produces byte-identical files even when its input vectors
//! arrive in a different order.
//!
//! The interface deliberately cannot carry arbitrary OOXML, PDF operators,
//! formulas, links, images, macros, templates, or style instructions. Those
//! details stay inside the file engine, where generation and validation are
//! one operation.

use std::collections::{BTreeMap, BTreeSet};

use crate::{
    generate, Block, Cell, Deck, Document, FileError, FileSpec, Format, GeneratedFile, InputIssue,
    LimitKind, Limits, PdfDocument, PdfPage, Sheet, Slide, Workbook,
};

const FINDINGS_PER_SLIDE: usize = 4;
const SOURCES_PER_SLIDE: usize = 8;
const PDF_LINES_PER_PAGE: usize = 36;

/// How one finding came to be.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum FindingBasis {
    /// Text the person entered directly. It may carry no external citation.
    User,
    /// A value read directly from a page.
    Page,
    /// A summary of cited page material.
    Summary,
    /// A conclusion derived from cited values.
    Inference,
}

impl FindingBasis {
    const fn label(self) -> &'static str {
        match self {
            Self::User => "You entered it",
            Self::Page => "Read from the page",
            Self::Summary => "Summarized from the page",
            Self::Inference => "Worked out from other values",
        }
    }
}

/// One source available to the report.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReportSource {
    /// Stable opaque identity used only to join findings to sources.
    pub key: String,
    /// Human-visible source title.
    pub title: String,
    /// Already-redacted human-visible location, such as a host.
    pub display_locator: String,
}

/// One accepted finding and its exact cited source identities.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReportFinding {
    /// Stable grouping name, such as a compared product.
    pub subject: String,
    /// Field or predicate name.
    pub field: String,
    /// Literal value. It is never interpreted as a formula or markup.
    pub value: String,
    /// Closed explanation of how the value was produced.
    pub basis: FindingBasis,
    /// Source keys. Required for every basis except [`FindingBasis::User`].
    pub source_keys: Vec<String>,
}

/// One deterministic source-bearing report.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SourcedReport {
    /// Trusted title derived from the workspace, not model-authored file data.
    pub title: String,
    /// Sources the findings may cite.
    pub sources: Vec<ReportSource>,
    /// Accepted findings.
    pub findings: Vec<ReportFinding>,
}

/// Generate and structurally validate one rich file with a source index.
pub fn generate_sourced_report(
    format: Format,
    report: &SourcedReport,
    limits: Limits,
) -> Result<GeneratedFile, FileError> {
    let prepared = PreparedReport::new(report, limits)?;
    generate(&prepared.file_spec(format, limits)?, limits)
}

struct PreparedReport<'a> {
    title: &'a str,
    sources: Vec<NumberedSource<'a>>,
    findings: Vec<PreparedFinding<'a>>,
}

struct NumberedSource<'a> {
    number: usize,
    source: &'a ReportSource,
}

struct PreparedFinding<'a> {
    finding: &'a ReportFinding,
    citations: Vec<usize>,
}

impl<'a> PreparedReport<'a> {
    fn new(report: &'a SourcedReport, limits: Limits) -> Result<Self, FileError> {
        if report.title.trim().is_empty() || report.findings.is_empty() {
            return Err(FileError::InvalidInput(InputIssue::EmptyRequiredValue));
        }
        preflight_input_size(report, limits)?;

        let mut sources = report.sources.iter().collect::<Vec<_>>();
        sources.sort_by(|left, right| {
            left.display_locator
                .cmp(&right.display_locator)
                .then_with(|| left.title.cmp(&right.title))
                .then_with(|| left.key.cmp(&right.key))
        });
        if sources.iter().any(|source| source.key.trim().is_empty()) {
            return Err(FileError::InvalidInput(InputIssue::InvalidSourceIdentity));
        }
        let mut by_key = BTreeMap::new();
        let numbered = sources
            .into_iter()
            .enumerate()
            .map(|(index, source)| {
                let number = index.saturating_add(1);
                if by_key.insert(source.key.as_str(), number).is_some() {
                    return Err(FileError::InvalidInput(InputIssue::InvalidSourceIdentity));
                }
                Ok(NumberedSource { number, source })
            })
            .collect::<Result<Vec<_>, _>>()?;

        let mut findings = Vec::with_capacity(report.findings.len());
        for finding in &report.findings {
            if finding.subject.trim().is_empty()
                || finding.field.trim().is_empty()
                || finding.value.trim().is_empty()
            {
                return Err(FileError::InvalidInput(InputIssue::EmptyRequiredValue));
            }
            let mut citations = BTreeSet::new();
            for key in &finding.source_keys {
                let Some(number) = by_key.get(key.as_str()).copied() else {
                    return Err(FileError::InvalidInput(
                        InputIssue::InvalidSourceAttribution,
                    ));
                };
                citations.insert(number);
            }
            if finding.basis != FindingBasis::User && citations.is_empty() {
                return Err(FileError::InvalidInput(
                    InputIssue::InvalidSourceAttribution,
                ));
            }
            findings.push(PreparedFinding {
                finding,
                citations: citations.into_iter().collect(),
            });
        }
        findings.sort_by(|left, right| {
            left.finding
                .subject
                .cmp(&right.finding.subject)
                .then_with(|| left.finding.field.cmp(&right.finding.field))
                .then_with(|| left.finding.value.cmp(&right.finding.value))
                .then_with(|| left.finding.basis.cmp(&right.finding.basis))
                .then_with(|| left.citations.cmp(&right.citations))
        });

        Ok(Self {
            title: &report.title,
            sources: numbered,
            findings,
        })
    }

    fn file_spec(&self, format: Format, limits: Limits) -> Result<FileSpec, FileError> {
        match format {
            Format::Xlsx => Ok(FileSpec::Workbook(self.workbook())),
            Format::Docx => Ok(FileSpec::Document(self.document())),
            Format::Pptx => self.deck(limits).map(FileSpec::Deck),
            Format::Pdf => self.pdf(limits).map(FileSpec::Pdf),
        }
    }

    fn workbook(&self) -> Workbook {
        let mut findings = vec![text_row(&[
            "Subject",
            "Field",
            "Value",
            "How found",
            "Sources",
        ])];
        findings.extend(self.findings.iter().map(|finding| {
            vec![
                Cell::Text(finding.finding.subject.clone()),
                Cell::Text(finding.finding.field.clone()),
                Cell::Text(finding.finding.value.clone()),
                Cell::Text(finding.finding.basis.label().to_owned()),
                Cell::Text(citation_text(&finding.citations)),
            ]
        }));
        let mut sources = vec![text_row(&["Source", "Title", "Location"])];
        sources.extend(self.sources.iter().map(|source| {
            vec![
                Cell::Text(source.number.to_string()),
                Cell::Text(source.source.title.clone()),
                Cell::Text(source.source.display_locator.clone()),
            ]
        }));
        Workbook {
            title: self.title.to_owned(),
            sheets: vec![
                Sheet {
                    name: "Findings".to_owned(),
                    rows: findings,
                },
                Sheet {
                    name: "Sources".to_owned(),
                    rows: sources,
                },
            ],
        }
    }

    fn document(&self) -> Document {
        let mut finding_rows = vec![vec![
            "Subject".to_owned(),
            "Field".to_owned(),
            "Value".to_owned(),
            "How found".to_owned(),
            "Sources".to_owned(),
        ]];
        finding_rows.extend(self.findings.iter().map(|finding| {
            vec![
                finding.finding.subject.clone(),
                finding.finding.field.clone(),
                finding.finding.value.clone(),
                finding.finding.basis.label().to_owned(),
                citation_text(&finding.citations),
            ]
        }));
        let mut source_rows = vec![vec![
            "Source".to_owned(),
            "Title".to_owned(),
            "Location".to_owned(),
        ]];
        source_rows.extend(self.sources.iter().map(|source| {
            vec![
                source.number.to_string(),
                source.source.title.clone(),
                source.source.display_locator.clone(),
            ]
        }));
        Document {
            title: self.title.to_owned(),
            blocks: vec![
                Block::Heading {
                    level: 1,
                    text: self.title.to_owned(),
                },
                Block::Heading {
                    level: 2,
                    text: "Findings".to_owned(),
                },
                Block::Table(finding_rows),
                Block::Heading {
                    level: 2,
                    text: "Sources".to_owned(),
                },
                Block::Table(source_rows),
            ],
        }
    }

    fn deck(&self, limits: Limits) -> Result<Deck, FileError> {
        let mut slides = vec![Slide {
            title: self.title.to_owned(),
            body: vec![
                format!("{} findings", self.findings.len()),
                format!("{} sources", self.sources.len()),
            ],
        }];
        for (index, chunk) in self.findings.chunks(FINDINGS_PER_SLIDE).enumerate() {
            slides.push(Slide {
                title: format!("Findings {}", index.saturating_add(1)),
                body: chunk
                    .iter()
                    .map(|finding| {
                        format!(
                            "{} — {}: {} ({})",
                            finding.finding.subject,
                            finding.finding.field,
                            finding.finding.value,
                            citation_text(&finding.citations)
                        )
                    })
                    .collect(),
            });
        }
        for (index, chunk) in self.sources.chunks(SOURCES_PER_SLIDE).enumerate() {
            slides.push(Slide {
                title: format!("Sources {}", index.saturating_add(1)),
                body: chunk
                    .iter()
                    .map(|source| {
                        format!(
                            "[{}] {} — {}",
                            source.number, source.source.title, source.source.display_locator
                        )
                    })
                    .collect(),
            });
        }
        if slides.len() > limits.max_slides {
            return Err(limit(LimitKind::Slides, limits.max_slides));
        }
        Ok(Deck {
            title: self.title.to_owned(),
            slides,
        })
    }

    fn pdf(&self, limits: Limits) -> Result<PdfDocument, FileError> {
        let per_page = limits.max_lines_per_page.min(PDF_LINES_PER_PAGE);
        if per_page == 0 {
            return Err(limit(LimitKind::Lines, limits.max_lines_per_page));
        }
        let mut lines = vec![self.title.to_owned(), "Findings".to_owned()];
        lines.extend(self.findings.iter().map(|finding| {
            format!(
                "{} - {}: {} ({})",
                finding.finding.subject,
                finding.finding.field,
                finding.finding.value,
                citation_text(&finding.citations)
            )
        }));
        lines.push("Sources".to_owned());
        lines.extend(self.sources.iter().map(|source| {
            format!(
                "[{}] {} - {}",
                source.number, source.source.title, source.source.display_locator
            )
        }));
        let pages = lines
            .chunks(per_page)
            .map(|chunk| PdfPage {
                lines: chunk.to_vec(),
            })
            .collect::<Vec<_>>();
        if pages.len() > limits.max_pages {
            return Err(limit(LimitKind::Pages, limits.max_pages));
        }
        Ok(PdfDocument {
            title: self.title.to_owned(),
            pages,
        })
    }
}

fn preflight_input_size(report: &SourcedReport, limits: Limits) -> Result<(), FileError> {
    let mut bytes = report.title.len();
    for source in &report.sources {
        for text in [&source.key, &source.title, &source.display_locator] {
            bytes = checked_text(bytes, text.len(), limits)?;
        }
    }
    for finding in &report.findings {
        for text in [&finding.subject, &finding.field, &finding.value] {
            bytes = checked_text(bytes, text.len(), limits)?;
        }
        for key in &finding.source_keys {
            bytes = checked_text(bytes, key.len(), limits)?;
        }
    }
    Ok(())
}

fn checked_text(current: usize, additional: usize, limits: Limits) -> Result<usize, FileError> {
    let next = current
        .checked_add(additional)
        .ok_or_else(|| limit(LimitKind::TextBytes, limits.max_text_bytes))?;
    if next > limits.max_text_bytes {
        return Err(limit(LimitKind::TextBytes, limits.max_text_bytes));
    }
    Ok(next)
}

fn text_row(values: &[&str]) -> Vec<Cell> {
    values
        .iter()
        .map(|value| Cell::Text((*value).to_owned()))
        .collect()
}

fn citation_text(citations: &[usize]) -> String {
    if citations.is_empty() {
        return "You".to_owned();
    }
    citations
        .iter()
        .map(|number| format!("[{number}]"))
        .collect::<Vec<_>>()
        .join(" ")
}

const fn limit(kind: LimitKind, maximum: usize) -> FileError {
    FileError::LimitExceeded { kind, maximum }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn report() -> SourcedReport {
        SourcedReport {
            title: "Comparison".to_owned(),
            sources: vec![
                ReportSource {
                    key: "b".to_owned(),
                    title: "Beta".to_owned(),
                    display_locator: "b.example".to_owned(),
                },
                ReportSource {
                    key: "a".to_owned(),
                    title: "Alpha".to_owned(),
                    display_locator: "a.example".to_owned(),
                },
            ],
            findings: vec![
                ReportFinding {
                    subject: "B".to_owned(),
                    field: "price".to_owned(),
                    value: "20".to_owned(),
                    basis: FindingBasis::Page,
                    source_keys: vec!["b".to_owned()],
                },
                ReportFinding {
                    subject: "A".to_owned(),
                    field: "price".to_owned(),
                    value: "10".to_owned(),
                    basis: FindingBasis::Page,
                    source_keys: vec!["a".to_owned()],
                },
            ],
        }
    }

    #[test]
    fn projection_numbers_sources_and_orders_findings() {
        let report = report();
        let prepared = PreparedReport::new(&report, Limits::default())
            .unwrap_or_else(|_| unreachable!("valid report prepares"));
        let Some(first_source) = prepared.sources.first() else {
            unreachable!("sample has a source");
        };
        assert_eq!(first_source.source.key, "a");
        assert_eq!(first_source.number, 1);
        let Some(first_finding) = prepared.findings.first() else {
            unreachable!("sample has a finding");
        };
        assert_eq!(first_finding.finding.subject, "A");
        assert_eq!(first_finding.citations, vec![1]);
        let FileSpec::Workbook(workbook) = prepared
            .file_spec(Format::Xlsx, Limits::default())
            .unwrap_or_else(|_| unreachable!("workbook projects"))
        else {
            unreachable!("xlsx projects to a workbook");
        };
        assert_eq!(workbook.sheets.len(), 2);
        assert_eq!(
            workbook.sheets.first().map(|sheet| sheet.name.as_str()),
            Some("Findings")
        );
        assert_eq!(
            workbook.sheets.get(1).map(|sheet| sheet.name.as_str()),
            Some("Sources")
        );
    }

    #[test]
    fn external_findings_are_never_allowed_to_lose_their_source() {
        let mut missing = report();
        let Some(finding) = missing.findings.first_mut() else {
            unreachable!("sample has a finding");
        };
        finding.source_keys.clear();
        assert_eq!(
            PreparedReport::new(&missing, Limits::default()).err(),
            Some(FileError::InvalidInput(
                InputIssue::InvalidSourceAttribution
            ))
        );

        let mut unknown = report();
        let Some(finding) = unknown.findings.first_mut() else {
            unreachable!("sample has a finding");
        };
        finding.source_keys = vec!["unknown".to_owned()];
        assert_eq!(
            PreparedReport::new(&unknown, Limits::default()).err(),
            Some(FileError::InvalidInput(
                InputIssue::InvalidSourceAttribution
            ))
        );
    }

    #[test]
    fn duplicate_source_identities_are_refused_before_numbering() {
        let mut duplicate = report();
        let Some(first_key) = duplicate.sources.first().map(|source| source.key.clone()) else {
            unreachable!("sample has a source");
        };
        let Some(second) = duplicate.sources.get_mut(1) else {
            unreachable!("sample has two sources");
        };
        second.key = first_key;
        assert_eq!(
            PreparedReport::new(&duplicate, Limits::default()).err(),
            Some(FileError::InvalidInput(InputIssue::InvalidSourceIdentity))
        );
    }
}
