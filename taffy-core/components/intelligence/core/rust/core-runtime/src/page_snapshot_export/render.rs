// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic serialization over decoded arena nodes.

mod canonical_json;
mod markdown;

use crate::context::PageArena;
use crate::wire;

pub(super) struct ExportMetadata<'a> {
    pub(super) origin: &'a str,
    pub(super) graph_revision: u64,
    pub(super) secure_context: bool,
    pub(super) node_count: u32,
    pub(super) redacted_field_count: u32,
    pub(super) suppressed_secret_value_count: u32,
    pub(super) withheld_field_count: u32,
    pub(super) captured_at_epoch_ms: u64,
    pub(super) source_query_withheld: bool,
    pub(super) source_fragment_withheld: bool,
}

pub(super) struct RenderedExport {
    pub(super) mime_type: &'static str,
    pub(super) file_name: &'static str,
    pub(super) content: Vec<u8>,
}

pub(super) fn render(
    format: wire::PageSnapshotExportFormat,
    arena: &PageArena,
    metadata: &ExportMetadata<'_>,
    max_bytes: u32,
) -> Result<RenderedExport, ()> {
    let limit = usize::try_from(max_bytes).map_err(|_| ())?;
    match format {
        wire::PageSnapshotExportFormat::Markdown => Ok(RenderedExport {
            mime_type: "text/markdown",
            file_name: "taffy-page-snapshot.md",
            content: markdown::render(arena, metadata, limit)?,
        }),
        wire::PageSnapshotExportFormat::CanonicalJson => Ok(RenderedExport {
            mime_type: "application/json",
            file_name: "taffy-page-snapshot.json",
            content: canonical_json::render(arena, metadata, limit)?,
        }),
    }
}

pub(super) struct BoundedOutput {
    bytes: Vec<u8>,
    limit: usize,
}

impl BoundedOutput {
    pub(super) fn new(limit: usize) -> Self {
        Self {
            bytes: Vec::with_capacity(limit.min(4096)),
            limit,
        }
    }

    pub(super) fn push(&mut self, value: &str) -> Result<(), ()> {
        self.push_bytes(value.as_bytes())
    }

    fn push_bytes(&mut self, value: &[u8]) -> Result<(), ()> {
        let length = self.bytes.len().checked_add(value.len()).ok_or(())?;
        if length > self.limit {
            return Err(());
        }
        self.bytes.extend_from_slice(value);
        Ok(())
    }

    pub(super) fn push_u64(&mut self, mut value: u64) -> Result<(), ()> {
        let mut digits = [0_u8; 20];
        let mut start = digits.len();
        loop {
            start = start.checked_sub(1).ok_or(())?;
            let digit = digits.get_mut(start).ok_or(())?;
            *digit = b'0'.saturating_add(u8::try_from(value % 10).map_err(|_| ())?);
            value /= 10;
            if value == 0 {
                break;
            }
        }
        self.push_bytes(digits.get(start..).ok_or(())?)
    }

    pub(super) fn finish(self) -> Vec<u8> {
        self.bytes
    }
}

#[cfg(test)]
mod tests {
    use super::BoundedOutput;

    #[test]
    fn integers_render_without_intermediate_strings() {
        let mut out = BoundedOutput::new(64);
        for (index, value) in [0, 9, 10, u64::MAX].into_iter().enumerate() {
            if index > 0 {
                assert_eq!(out.push(","), Ok(()));
            }
            assert_eq!(out.push_u64(value), Ok(()));
        }

        assert_eq!(out.finish(), b"0,9,10,18446744073709551615");
    }

    #[test]
    fn an_integer_over_the_output_bound_writes_nothing() {
        let mut out = BoundedOutput::new(1);

        assert_eq!(out.push_u64(10), Err(()));
        assert!(out.finish().is_empty());
    }
}
