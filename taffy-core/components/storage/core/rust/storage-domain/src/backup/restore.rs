// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Restore planning and staged-commit state.
//!
//! Existing records are never silently overwritten. A deletion always wins
//! over an older active snapshot, and a differing live revision is surfaced as
//! a conflict for an explicit import/replace decision above this layer.

use std::collections::BTreeMap;

use super::{
    BackupDigest, BackupError, BackupManifest, BackupRecord, BackupRecordKind, BackupRecordState,
    MAX_BACKUP_RECORDS,
};

mod session;

pub use session::{RestoreSession, RestoreSessionPhase};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CurrentRecord {
    pub kind: BackupRecordKind,
    pub stable_id: String,
    pub revision: u64,
    pub schema_version: u32,
    pub state: BackupRecordState,
    pub plaintext_bytes: u64,
    pub plaintext_sha256: [u8; 32],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RestoreAction {
    StageCreate,
    StageDeletion,
    AlreadyPresent,
    KeepNewerCurrent,
    BlockedByDeletion,
    NeedsExplicitConflictChoice,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RestoreEntry {
    pub kind: BackupRecordKind,
    pub stable_id: String,
    pub archive_revision: u64,
    pub action: RestoreAction,
    pub schema_version: u32,
    pub state: BackupRecordState,
    pub plaintext_bytes: u64,
    pub plaintext_sha256: [u8; 32],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RestoreTargetKind {
    NewRegularProfile,
    ExistingRegularProfile,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RestoreTarget {
    pub kind: RestoreTargetKind,
    /// An opaque browser-owned profile identity, never a filesystem path.
    pub profile_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RestorePlan {
    pub(super) backup_id: String,
    pub(super) snapshot_sha256: [u8; 32],
    pub(super) target: RestoreTarget,
    pub(super) entries: Vec<RestoreEntry>,
}

impl RestorePlan {
    #[must_use]
    pub fn backup_id(&self) -> &str {
        &self.backup_id
    }

    #[must_use]
    pub fn snapshot_sha256(&self) -> [u8; 32] {
        self.snapshot_sha256
    }

    #[must_use]
    pub fn target(&self) -> &RestoreTarget {
        &self.target
    }

    #[must_use]
    pub fn entries(&self) -> &[RestoreEntry] {
        &self.entries
    }

    #[must_use]
    pub fn has_conflicts(&self) -> bool {
        self.entries.iter().any(|entry| {
            matches!(
                entry.action,
                RestoreAction::BlockedByDeletion | RestoreAction::NeedsExplicitConflictChoice
            )
        })
    }

    pub fn confirmation_digest(&self, digest: &dyn BackupDigest) -> Result<[u8; 32], BackupError> {
        self.validate()?;
        let material = super::plan_codec::encode_confirmation_material(self)?;
        let calculated = digest.sha256(&material)?;
        if calculated.iter().all(|byte| *byte == 0) {
            return Err(BackupError::InvalidDigest);
        }
        Ok(calculated)
    }

    fn validate(&self) -> Result<(), BackupError> {
        super::validate_id("backup id", &self.backup_id)?;
        super::validate_id("target profile id", &self.target.profile_id)?;
        if self.snapshot_sha256.iter().all(|byte| *byte == 0) {
            return Err(BackupError::InvalidDigest);
        }
        if self.entries.len() > MAX_BACKUP_RECORDS {
            return Err(BackupError::TooManyRecords);
        }
        let mut identities = std::collections::BTreeSet::new();
        for entry in &self.entries {
            super::validate_id("restore record id", &entry.stable_id)?;
            if entry.archive_revision == 0
                || entry.schema_version == 0
                || entry.plaintext_bytes > super::MAX_BACKUP_RECORD_BYTES
                || (entry.state == BackupRecordState::Active
                    && (entry.plaintext_bytes == 0
                        || entry.plaintext_sha256.iter().all(|byte| *byte == 0)))
                || (entry.state == BackupRecordState::Tombstone
                    && (entry.plaintext_bytes != 0
                        || entry.plaintext_sha256.iter().any(|byte| *byte != 0)))
                || !identities.insert((entry.kind, entry.stable_id.as_str()))
            {
                return Err(BackupError::RestoreConflict);
            }
            if self.target.kind == RestoreTargetKind::NewRegularProfile
                && !matches!(
                    entry.action,
                    RestoreAction::StageCreate | RestoreAction::StageDeletion
                )
            {
                return Err(BackupError::RestoreConflict);
            }
        }
        Ok(())
    }
}

pub(super) fn plan_verified_restore(
    manifest: &BackupManifest,
    current: &[CurrentRecord],
    target: RestoreTarget,
) -> Result<RestorePlan, BackupError> {
    super::validate_id("target profile id", &target.profile_id)?;
    if current.len() > MAX_BACKUP_RECORDS {
        return Err(BackupError::TooManyRecords);
    }
    if target.kind == RestoreTargetKind::NewRegularProfile && !current.is_empty() {
        return Err(BackupError::RestoreConflict);
    }
    for record in current {
        super::validate_id("current record id", &record.stable_id)?;
        if record.revision == 0
            || record.schema_version == 0
            || record.plaintext_bytes > super::MAX_BACKUP_RECORD_BYTES
            || (record.state == BackupRecordState::Active && record.plaintext_bytes == 0)
            || (record.state == BackupRecordState::Active
                && record.plaintext_sha256.iter().all(|byte| *byte == 0))
            || (record.state == BackupRecordState::Tombstone && record.plaintext_bytes != 0)
            || (record.state == BackupRecordState::Tombstone
                && record.plaintext_sha256.iter().any(|byte| *byte != 0))
        {
            return Err(BackupError::RestoreConflict);
        }
    }
    let mut current_by_id = BTreeMap::new();
    for record in current {
        if current_by_id
            .insert((record.kind, record.stable_id.as_str()), record)
            .is_some()
        {
            return Err(BackupError::RestoreConflict);
        }
    }

    let entries = manifest
        .records
        .iter()
        .map(|archived| RestoreEntry {
            kind: archived.kind,
            stable_id: archived.stable_id.clone(),
            archive_revision: archived.revision,
            action: action_for(
                archived,
                current_by_id.get(&(archived.kind, archived.stable_id.as_str())),
            ),
            schema_version: archived.schema_version,
            state: archived.state,
            plaintext_bytes: archived.plaintext_bytes,
            plaintext_sha256: archived.plaintext_sha256,
        })
        .collect();
    let plan = RestorePlan {
        backup_id: manifest.backup_id.clone(),
        snapshot_sha256: manifest.snapshot_sha256,
        target,
        entries,
    };
    plan.validate()?;
    Ok(plan)
}

fn action_for(archived: &BackupRecord, current: Option<&&CurrentRecord>) -> RestoreAction {
    let Some(current) = current else {
        return match archived.state {
            BackupRecordState::Active => RestoreAction::StageCreate,
            BackupRecordState::Tombstone => RestoreAction::StageDeletion,
        };
    };

    if archived.state == BackupRecordState::Active
        && current.state == BackupRecordState::Tombstone
        && archived.revision < current.revision
    {
        return RestoreAction::BlockedByDeletion;
    }
    if archived.state == BackupRecordState::Tombstone
        && current.state == BackupRecordState::Tombstone
    {
        return match archived.revision.cmp(&current.revision) {
            std::cmp::Ordering::Less => RestoreAction::KeepNewerCurrent,
            std::cmp::Ordering::Equal if archived.schema_version == current.schema_version => {
                RestoreAction::AlreadyPresent
            }
            std::cmp::Ordering::Equal => RestoreAction::NeedsExplicitConflictChoice,
            std::cmp::Ordering::Greater => RestoreAction::StageDeletion,
        };
    }
    if archived.state == BackupRecordState::Tombstone && current.state == BackupRecordState::Active
    {
        return match archived.revision.cmp(&current.revision) {
            std::cmp::Ordering::Less => RestoreAction::KeepNewerCurrent,
            std::cmp::Ordering::Greater => RestoreAction::StageDeletion,
            std::cmp::Ordering::Equal => RestoreAction::NeedsExplicitConflictChoice,
        };
    }
    if archived.state == BackupRecordState::Active && current.state == BackupRecordState::Active {
        if archived.revision < current.revision {
            return RestoreAction::KeepNewerCurrent;
        }
        if archived.revision == current.revision
            && archived.schema_version == current.schema_version
            && archived.plaintext_bytes == current.plaintext_bytes
            && archived.plaintext_sha256 == current.plaintext_sha256
        {
            return RestoreAction::AlreadyPresent;
        }
    }
    RestoreAction::NeedsExplicitConflictChoice
}
