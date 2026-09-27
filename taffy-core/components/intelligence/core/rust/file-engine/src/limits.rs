// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::{FileError, InputIssue};

/// Explicit resource ceilings for one generation or validation.
///
/// Defaults target an interactive phone task rather than format maxima. A
/// caller may lower any field. The implementation never silently raises one.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Limits {
    /// Maximum final file and any one intermediate part, in bytes.
    pub max_output_bytes: usize,
    /// Maximum UTF-8 source text bytes across the request.
    pub max_text_bytes: usize,
    /// Maximum workbook sheets.
    pub max_sheets: usize,
    /// Maximum rows in one sheet.
    pub max_rows: usize,
    /// Maximum columns in one row.
    pub max_columns: usize,
    /// Maximum non-blank cells across a workbook.
    pub max_cells: usize,
    /// Maximum blocks in a document.
    pub max_blocks: usize,
    /// Maximum slides in a deck.
    pub max_slides: usize,
    /// Maximum pages in a PDF.
    pub max_pages: usize,
    /// Maximum text lines on one PDF page.
    pub max_lines_per_page: usize,
    /// Maximum entries in an OOXML package.
    pub max_package_parts: usize,
}

impl Limits {
    /// Conservative interactive defaults.
    pub const DEFAULT: Self = Self {
        max_output_bytes: 8 * 1024 * 1024,
        max_text_bytes: 512 * 1024,
        max_sheets: 32,
        max_rows: 10_000,
        max_columns: 256,
        max_cells: 100_000,
        max_blocks: 2_048,
        max_slides: 128,
        max_pages: 256,
        max_lines_per_page: 512,
        max_package_parts: 512,
    };

    pub(crate) fn validate(self) -> Result<(), FileError> {
        let values = [
            self.max_output_bytes,
            self.max_text_bytes,
            self.max_sheets,
            self.max_rows,
            self.max_columns,
            self.max_cells,
            self.max_blocks,
            self.max_slides,
            self.max_pages,
            self.max_lines_per_page,
            self.max_package_parts,
        ];
        if values.into_iter().any(|value| value == 0)
            || self.max_sheets > u16::MAX.into()
            || self.max_package_parts > u16::MAX.into()
            || self.max_output_bytes > u32::MAX as usize
        {
            return Err(FileError::InvalidInput(InputIssue::InvalidLimits));
        }
        Ok(())
    }
}

impl Default for Limits {
    fn default() -> Self {
        Self::DEFAULT
    }
}
