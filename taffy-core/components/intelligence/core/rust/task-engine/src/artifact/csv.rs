// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Rendering the comma-separated artifact.
//!
//! One responsibility: the column order and the header row. Every value goes
//! through [`super::cell::csv_cell`], which is what keeps a value a
//! spreadsheet would evaluate as a formula from ever being written as one.

use super::cell::csv_cell;
use super::layout::{citation_numbers, classification_text, NumberedSource, Row};
use super::writer::ArtifactWriter;
pub(super) fn render(
    sources: &[NumberedSource<'_>],
    rows: &[Row<'_>],
    limit: u64,
) -> ArtifactWriter {
    let mut out = ArtifactWriter::new(limit);
    out.push_str(
        "subject,detail,value,unit,how_it_was_found,confidence_basis_points,observed_at,sensitivity,sources,source_locators\n",
    );
    for row in rows {
        let mut locators = String::new();
        for number in &row.citations {
            let Some(source) = number.checked_sub(1).and_then(|index| sources.get(index)) else {
                continue;
            };
            if !locators.is_empty() {
                locators.push(' ');
            }
            locators.push_str(&source.source.display_locator);
        }
        let citations = citation_numbers(&row.citations);
        append_line!(
            out,
            "{},{},{},{},{},{},{},{},{},{}\n",
            csv_cell(&row.fact.subject_key),
            csv_cell(&row.fact.predicate),
            csv_cell(&row.fact.typed_value),
            csv_cell(row.fact.unit.as_deref().unwrap_or("")),
            csv_cell(classification_text(row.fact.classification)),
            csv_cell(
                &row.fact
                    .confidence_basis_points
                    .map_or_else(String::new, |points| points.to_string())
            ),
            csv_cell(row.fact.observation_time.as_str()),
            csv_cell(row.fact.sensitivity.as_str()),
            csv_cell(&citations),
            csv_cell(&locators),
        );
    }
    out
}
