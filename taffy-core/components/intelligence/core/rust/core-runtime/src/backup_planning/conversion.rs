// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exhaustive conversion between generated wire values and backup-domain values.

use taffy_storage::backup::{
    BackupRecord, BackupRecordKind, BackupRecordState, CurrentRecord, RestoreAction,
    RestoreTargetKind,
};

use crate::wire;

pub(super) fn record_to_domain(record: wire::BackupRecordDescriptor) -> BackupRecord {
    BackupRecord {
        kind: record_kind_to_domain(record.kind),
        stable_id: record.stable_id,
        revision: record.revision,
        schema_version: record.schema_version,
        state: record_state_to_domain(record.state),
        plaintext_bytes: record.plaintext_bytes,
        plaintext_sha256: record.plaintext_sha256,
    }
}

pub(super) fn current_record_to_domain(record: wire::BackupRecordDescriptor) -> CurrentRecord {
    CurrentRecord {
        kind: record_kind_to_domain(record.kind),
        stable_id: record.stable_id,
        revision: record.revision,
        schema_version: record.schema_version,
        state: record_state_to_domain(record.state),
        plaintext_bytes: record.plaintext_bytes,
        plaintext_sha256: record.plaintext_sha256,
    }
}

pub(super) const fn record_kind_to_domain(kind: wire::BackupRecordKind) -> BackupRecordKind {
    match kind {
        wire::BackupRecordKind::AssistantConfiguration => BackupRecordKind::AssistantConfiguration,
        wire::BackupRecordKind::SavedWorkspace => BackupRecordKind::SavedWorkspace,
        wire::BackupRecordKind::LibraryEntry => BackupRecordKind::LibraryEntry,
        wire::BackupRecordKind::MemoryRecord => BackupRecordKind::MemoryRecord,
        wire::BackupRecordKind::UserAuthoredSkill => BackupRecordKind::UserAuthoredSkill,
        wire::BackupRecordKind::LearnedProcedure => BackupRecordKind::LearnedProcedure,
        wire::BackupRecordKind::Bookmark => BackupRecordKind::Bookmark,
        wire::BackupRecordKind::BrowserPreference => BackupRecordKind::BrowserPreference,
    }
}

pub(super) const fn record_kind_from_domain(kind: BackupRecordKind) -> wire::BackupRecordKind {
    match kind {
        BackupRecordKind::AssistantConfiguration => wire::BackupRecordKind::AssistantConfiguration,
        BackupRecordKind::SavedWorkspace => wire::BackupRecordKind::SavedWorkspace,
        BackupRecordKind::LibraryEntry => wire::BackupRecordKind::LibraryEntry,
        BackupRecordKind::MemoryRecord => wire::BackupRecordKind::MemoryRecord,
        BackupRecordKind::UserAuthoredSkill => wire::BackupRecordKind::UserAuthoredSkill,
        BackupRecordKind::LearnedProcedure => wire::BackupRecordKind::LearnedProcedure,
        BackupRecordKind::Bookmark => wire::BackupRecordKind::Bookmark,
        BackupRecordKind::BrowserPreference => wire::BackupRecordKind::BrowserPreference,
    }
}

const fn record_state_to_domain(state: wire::BackupRecordState) -> BackupRecordState {
    match state {
        wire::BackupRecordState::Active => BackupRecordState::Active,
        wire::BackupRecordState::Tombstone => BackupRecordState::Tombstone,
    }
}

pub(super) const fn record_state_from_domain(state: BackupRecordState) -> wire::BackupRecordState {
    match state {
        BackupRecordState::Active => wire::BackupRecordState::Active,
        BackupRecordState::Tombstone => wire::BackupRecordState::Tombstone,
    }
}

pub(super) const fn target_kind_to_domain(
    kind: wire::BackupRestoreTargetKind,
) -> RestoreTargetKind {
    match kind {
        wire::BackupRestoreTargetKind::NewRegularProfile => RestoreTargetKind::NewRegularProfile,
        wire::BackupRestoreTargetKind::ExistingRegularProfile => {
            RestoreTargetKind::ExistingRegularProfile
        }
    }
}

pub(super) const fn target_kind_from_domain(
    kind: RestoreTargetKind,
) -> wire::BackupRestoreTargetKind {
    match kind {
        RestoreTargetKind::NewRegularProfile => wire::BackupRestoreTargetKind::NewRegularProfile,
        RestoreTargetKind::ExistingRegularProfile => {
            wire::BackupRestoreTargetKind::ExistingRegularProfile
        }
    }
}

pub(super) const fn action_from_domain(action: RestoreAction) -> wire::BackupRestoreAction {
    match action {
        RestoreAction::StageCreate => wire::BackupRestoreAction::StageCreate,
        RestoreAction::StageDeletion => wire::BackupRestoreAction::StageDeletion,
        RestoreAction::AlreadyPresent => wire::BackupRestoreAction::AlreadyPresent,
        RestoreAction::KeepNewerCurrent => wire::BackupRestoreAction::KeepNewerCurrent,
        RestoreAction::BlockedByDeletion => wire::BackupRestoreAction::BlockedByDeletion,
        RestoreAction::NeedsExplicitConflictChoice => {
            wire::BackupRestoreAction::NeedsExplicitConflictChoice
        }
    }
}
