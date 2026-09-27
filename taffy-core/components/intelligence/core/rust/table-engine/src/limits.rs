// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/// Every resource ceiling used by parsing, reshaping, lineage, and export.
///
/// Callers may lower these values. Raising them still cannot bypass the
/// process-level limits applied by the caller that transports the result.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Limits {
    pub max_input_bytes: usize,
    pub max_recipe_bytes: usize,
    pub max_output_bytes: usize,
    pub max_rows: usize,
    pub max_columns: usize,
    pub max_cells: usize,
    pub max_cell_bytes: usize,
    pub max_steps: usize,
    pub max_lineage_per_cell: usize,
    pub max_total_lineage: usize,
}

impl Default for Limits {
    fn default() -> Self {
        Self {
            max_input_bytes: 1024 * 1024,
            max_recipe_bytes: 64 * 1024,
            max_output_bytes: 1024 * 1024,
            max_rows: 4_096,
            max_columns: 128,
            max_cells: 262_144,
            max_cell_bytes: 16_384,
            max_steps: 16,
            max_lineage_per_cell: 4_096,
            max_total_lineage: 1_048_576,
        }
    }
}
