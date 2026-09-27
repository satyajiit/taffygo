// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded, deterministic person-requested Library exports.

use core_api_types::{WorkspaceExportFormat, MAX_EXPORT_CONTENT_BYTES};
use taffy_storage::library::LibraryEntry;

use crate::ports::LibraryStoreError;

pub(super) fn render_export<'a>(
    entries: impl Iterator<Item = &'a LibraryEntry>,
    collection_id: Option<&str>,
    format: WorkspaceExportFormat,
) -> Result<String, LibraryStoreError> {
    let entries = entries
        .filter(|entry| collection_id.is_none_or(|id| entry.collection_id.to_text() == id))
        .collect::<Vec<_>>();
    let content = match format {
        WorkspaceExportFormat::Markdown => render_markdown(&entries),
        WorkspaceExportFormat::Csv => render_csv(&entries),
    };
    if content.len() > MAX_EXPORT_CONTENT_BYTES {
        Err(LibraryStoreError::ExportOverflow)
    } else {
        Ok(content)
    }
}

fn render_markdown(entries: &[&LibraryEntry]) -> String {
    let mut content = String::from("# Library\n");
    for entry in entries {
        content.push_str("\n## ");
        content.push_str(&entry.collection_name);
        content.push_str(" — ");
        content.push_str(&entry.field);
        content.push_str("\n\n");
        content.push_str(entry.display_value());
        content.push_str("\n\nSources:\n");
        for source in &entry.sources {
            content.push_str("- ");
            content.push_str(&source.title);
            content.push_str(" (");
            content.push_str(&source.host);
            content.push_str(")\n");
        }
    }
    content
}

fn render_csv(entries: &[&LibraryEntry]) -> String {
    let mut content = String::from(
        "collection,field,value,original_value,source_hosts,captured_at_epoch_ms,last_checked_epoch_ms,has_conflict\n",
    );
    for entry in entries {
        let hosts = entry
            .sources
            .iter()
            .map(|source| source.host.as_str())
            .collect::<Vec<_>>()
            .join("; ");
        let values = [
            entry.collection_name.clone(),
            entry.field.clone(),
            entry.display_value().to_owned(),
            entry.original_value.clone(),
            hosts,
            entry.captured_at_epoch_ms.to_string(),
            entry.last_checked_epoch_ms.to_string(),
            entry.has_conflict.to_string(),
        ];
        content.push_str(
            &values
                .iter()
                .map(|value| csv_cell(value))
                .collect::<Vec<_>>()
                .join(","),
        );
        content.push('\n');
    }
    content
}

fn csv_cell(value: &str) -> String {
    let mut safe = if value.starts_with(['=', '+', '-', '@']) {
        format!("'{value}")
    } else {
        value.to_owned()
    };
    safe = safe.replace('"', "\"\"");
    format!("\"{safe}\"")
}
