// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Rendering the Markdown artifact.
//!
//! One responsibility: the Markdown layout. It orders nothing — the material
//! arrives already ordered from [`super::layout`] — and it escapes nothing
//! itself: every value goes through [`super::cell::markdown_cell`], so there
//! is no path by which a value reaches the document unescaped.

use super::cell::markdown_cell;
use super::layout::{citation_text, classification_text, provenance_text, NumberedSource, Row};
use super::request::ArtifactRequest;
use super::writer::ArtifactWriter;
pub(super) fn render(
    request: &ArtifactRequest,
    sources: &[NumberedSource<'_>],
    rows: &[Row<'_>],
    limit: u64,
) -> ArtifactWriter {
    let mut out = ArtifactWriter::new(limit);
    out.push_str("# ");
    out.push_str(&markdown_cell(&request.title));
    out.push_str("\n\n");
    out.push_str(
        "Every value below is a stored fact, and every citation is stored evidence.\nNothing here is written by a model.\n\n",
    );
    append_line!(out, "- Values: {}\n", rows.len());
    append_line!(out, "- Sources: {}\n", sources.len());
    append_line!(out, "- Output schema: {}\n\n", request.schema_version);

    out.push_str("## Findings\n\n");
    out.push_str("| Subject | Detail | Value | How it was found | Observed | Sources |\n");
    out.push_str("| --- | --- | --- | --- | --- | --- |\n");
    for row in rows {
        let value = match row.fact.unit.as_deref() {
            Some(unit) if !unit.is_empty() => format!("{} {unit}", row.fact.typed_value),
            _ => row.fact.typed_value.clone(),
        };
        append_line!(
            out,
            "| {} | {} | {} | {} | {} | {} |\n",
            markdown_cell(&row.fact.subject_key),
            markdown_cell(&row.fact.predicate),
            markdown_cell(&value),
            classification_text(row.fact.classification),
            markdown_cell(row.fact.observation_time.as_str()),
            citation_text(&row.citations),
        );
    }

    out.push_str("\n## Evidence\n\n");
    out.push_str("| Subject | Detail | Source | Where | Kind | Captured |\n");
    out.push_str("| --- | --- | --- | --- | --- | --- |\n");
    for row in rows {
        for evidence in &row.provenance {
            let locator = evidence.locator;
            append_line!(
                out,
                "| {} | {} | {} | {} | {} | {} |\n",
                markdown_cell(&row.fact.subject_key),
                markdown_cell(&row.fact.predicate),
                format!("[{}]", evidence.number),
                markdown_cell(locator.location_descriptor.as_deref().unwrap_or("")),
                provenance_text(locator.kind),
                markdown_cell(locator.captured_at.as_str()),
            );
        }
    }

    out.push_str("\n## Sources\n\n");
    for numbered in sources {
        let observed = numbered
            .source
            .last_observed_at
            .as_ref()
            .unwrap_or(&numbered.source.first_seen_at);
        append_line!(
            out,
            "{}. `{}` — {} (observed {})\n",
            numbered.number,
            markdown_cell(&numbered.source.display_locator),
            markdown_cell(numbered.source.title.as_deref().unwrap_or("")),
            markdown_cell(observed.as_str()),
        );
    }

    out.push_str(
        "\nDeleting this workspace does not delete a copy you have already saved or sent\nsomewhere else.\n",
    );
    out
}
