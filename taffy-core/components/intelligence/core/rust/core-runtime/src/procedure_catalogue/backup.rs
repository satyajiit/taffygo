// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Import admission uses the bootstrap validator, never a second TFSK reader.

use core_service_types as wire;
use task_engine::Milestone;

use super::{ProcedureCatalogue, ProcedureCatalogueError};

/// Validates the complete selected procedure set before a hidden candidate
/// can receive commit authority. The caller binds these records to its exact
/// retained restore plan; this function decides only whether that set can be
/// restored by the product's normal bootstrap.
///
/// This does not replace the source catalogue, start the target runtime,
/// restore run history, install a procedure, or grant execution authority.
/// Draft and disabled records are validated too: not being runnable does not
/// make a malformed stored definition safe to publish. An empty selection is
/// valid. Every nonempty set is all-or-nothing, including duplicate identities
/// across authored and learned records.
pub fn validate_backup_procedures(
    skills: Vec<wire::SkillRecord>,
) -> Result<(), ProcedureCatalogueError> {
    // Bootstrap has no task milestone and uses the same complete vocabulary.
    // Task admission still validates any later execution against its own
    // milestone, scope and reviewed authority.
    ProcedureCatalogue::restore(skills, Vec::new(), Milestone::M8).map(|_| ())
}

#[cfg(test)]
mod tests;
