// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical material for the opaque confirmation bound to one restore plan.

use super::{BackupError, BackupRecordKind, RestoreAction, RestorePlan, RestoreTargetKind};

const PLAN_MAGIC: [u8; 4] = *b"AIBP";
const PLAN_FORMAT_VERSION: u16 = 1;

pub(super) fn encode_confirmation_material(plan: &RestorePlan) -> Result<Vec<u8>, BackupError> {
    let mut encoded = Vec::with_capacity(encoded_size(plan)?);
    encoded.extend_from_slice(&PLAN_MAGIC);
    encoded.extend_from_slice(&PLAN_FORMAT_VERSION.to_le_bytes());
    encoded.push(target_wire(plan.target.kind));
    encoded.push(0);
    push_text(&mut encoded, &plan.backup_id)?;
    push_text(&mut encoded, &plan.target.profile_id)?;
    encoded.extend_from_slice(&plan.snapshot_sha256);
    let count = u32::try_from(plan.entries.len()).map_err(|_| BackupError::TooManyRecords)?;
    encoded.extend_from_slice(&count.to_le_bytes());
    for entry in &plan.entries {
        encoded.push(kind_wire(entry.kind));
        encoded.push(action_wire(entry.action));
        encoded.extend_from_slice(&0_u16.to_le_bytes());
        push_text(&mut encoded, &entry.stable_id)?;
        encoded.extend_from_slice(&entry.archive_revision.to_le_bytes());
    }
    Ok(encoded)
}

fn encoded_size(plan: &RestorePlan) -> Result<usize, BackupError> {
    let mut bytes = 4_usize
        .checked_add(2 + 1 + 1 + 32 + 4)
        .ok_or(BackupError::ArchiveTooLarge)?;
    for text in [plan.backup_id.as_str(), plan.target.profile_id.as_str()] {
        bytes = bytes
            .checked_add(2)
            .and_then(|value| value.checked_add(text.len()))
            .ok_or(BackupError::ArchiveTooLarge)?;
    }
    for entry in &plan.entries {
        bytes = bytes
            .checked_add(1 + 1 + 2 + 2 + 8)
            .and_then(|value| value.checked_add(entry.stable_id.len()))
            .ok_or(BackupError::ArchiveTooLarge)?;
    }
    Ok(bytes)
}

fn push_text(encoded: &mut Vec<u8>, value: &str) -> Result<(), BackupError> {
    let length = u16::try_from(value.len()).map_err(|_| BackupError::ArchiveTooLarge)?;
    encoded.extend_from_slice(&length.to_le_bytes());
    encoded.extend_from_slice(value.as_bytes());
    Ok(())
}

fn target_wire(kind: RestoreTargetKind) -> u8 {
    match kind {
        RestoreTargetKind::NewRegularProfile => 0,
        RestoreTargetKind::ExistingRegularProfile => 1,
    }
}

fn kind_wire(kind: BackupRecordKind) -> u8 {
    match kind {
        BackupRecordKind::AssistantConfiguration => 0,
        BackupRecordKind::SavedWorkspace => 1,
        BackupRecordKind::LibraryEntry => 2,
        BackupRecordKind::MemoryRecord => 3,
        BackupRecordKind::UserAuthoredSkill => 4,
        BackupRecordKind::LearnedProcedure => 5,
        BackupRecordKind::Bookmark => 6,
        BackupRecordKind::BrowserPreference => 7,
    }
}

fn action_wire(action: RestoreAction) -> u8 {
    match action {
        RestoreAction::StageCreate => 0,
        RestoreAction::StageDeletion => 1,
        RestoreAction::AlreadyPresent => 2,
        RestoreAction::KeepNewerCurrent => 3,
        RestoreAction::BlockedByDeletion => 4,
        RestoreAction::NeedsExplicitConflictChoice => 5,
    }
}
