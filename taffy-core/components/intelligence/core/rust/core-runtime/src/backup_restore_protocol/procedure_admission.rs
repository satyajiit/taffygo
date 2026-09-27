// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The staged procedure set must be exactly the set in the retained plan.

use std::collections::BTreeMap;

use taffy_storage::backup::{BackupRecordKind, BackupRecordState, RestoreAction, RestoreEntry};

use super::BackupRestoreProtocolError;
use crate::procedure_catalogue::validate_backup_procedures;
use crate::wire;

pub(super) fn validate_plan_procedures(
    entries: &[RestoreEntry],
    skills: Vec<wire::SkillRecord>,
) -> Result<(), BackupRestoreProtocolError> {
    let refused = BackupRestoreProtocolError::SnapshotMismatch;
    if skills.len() > wire::MAX_SKILLS_PER_PROFILE {
        return Err(refused);
    }
    let mut expected = BTreeMap::new();
    for entry in entries {
        let provenance = match entry.kind {
            BackupRecordKind::UserAuthoredSkill => wire::SkillProvenance::Authored,
            BackupRecordKind::LearnedProcedure => wire::SkillProvenance::RecordedFromTask,
            _ => continue,
        };
        // Only the new-profile path owns this protocol. Its current typed
        // adapters restore active V1 records; there is no skill tombstone
        // representation and no existing target catalogue to merge with.
        if entry.action != RestoreAction::StageCreate
            || entry.state != BackupRecordState::Active
            || entry.schema_version != 1
            || expected.len() >= wire::MAX_SKILLS_PER_PROFILE
            || expected
                .insert(
                    entry.stable_id.as_str(),
                    (entry.archive_revision, provenance),
                )
                .is_some()
        {
            return Err(refused);
        }
    }
    if expected.len() != skills.len() {
        return Err(refused);
    }
    for skill in &skills {
        let Some((revision, provenance)) = expected.remove(skill.skill_id.as_str()) else {
            return Err(refused);
        };
        if revision != u64::from(skill.active_version) || provenance != skill.provenance {
            return Err(refused);
        }
    }
    // The trusted browser stage owns the association between these decoded
    // records and the exact staged-payload digest. Core independently checks
    // completeness, identity and full portable definition admission; it never
    // treats native header validation as language validation.
    validate_backup_procedures(skills).map_err(|_| refused)
}

#[cfg(test)]
mod tests;
