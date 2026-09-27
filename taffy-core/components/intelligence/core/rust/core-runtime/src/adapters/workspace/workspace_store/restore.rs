// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Atomic restore from browser-owned workspace records.

use std::collections::BTreeMap;

use taffy_storage::workspace::decode_snapshot;

use super::{WorkspaceStore, MAX_RESTORE_BYTES, MAX_WORKSPACES};
use crate::ports::WorkspaceStoreError;

impl WorkspaceStore {
    pub fn restore_encoded(&mut self, snapshots: &[Vec<u8>]) -> Result<(), WorkspaceStoreError> {
        let records = snapshots
            .iter()
            .map(|snapshot| (None, None, snapshot.as_slice()))
            .collect::<Vec<_>>();
        self.restore_records(&records)
    }

    /// Restores browser-owned records while checking their indexed identity and revision.
    pub fn restore_bound_records(
        &mut self,
        records: &[(String, u64, Vec<u8>)],
    ) -> Result<(), WorkspaceStoreError> {
        let records = records
            .iter()
            .map(|(workspace_id, revision, snapshot)| {
                (
                    Some(workspace_id.as_str()),
                    Some(*revision),
                    snapshot.as_slice(),
                )
            })
            .collect::<Vec<_>>();
        self.restore_records(&records)
    }

    fn restore_records(
        &mut self,
        records: &[(Option<&str>, Option<u64>, &[u8])],
    ) -> Result<(), WorkspaceStoreError> {
        if records.len() > MAX_WORKSPACES {
            return Err(WorkspaceStoreError::TooManyWorkspaces);
        }
        let total_bytes = records.iter().try_fold(0usize, |total, (_, _, snapshot)| {
            total.checked_add(snapshot.len())
        });
        if total_bytes.is_none_or(|total| total > MAX_RESTORE_BYTES) {
            return Err(WorkspaceStoreError::TooManyWorkspaceBytes);
        }
        let mut restored = BTreeMap::new();
        for (indexed_id, indexed_revision, encoded) in records {
            let snapshot =
                decode_snapshot(encoded).map_err(|_| WorkspaceStoreError::InvalidSnapshot)?;
            let key = snapshot.workspace_id.to_text();
            if indexed_id.is_some_and(|value| value != key)
                || indexed_revision.is_some_and(|value| value != snapshot.revision)
            {
                return Err(WorkspaceStoreError::RestoreBindingMismatch);
            }
            if restored.insert(key, snapshot).is_some() {
                return Err(WorkspaceStoreError::DuplicateWorkspace);
            }
        }
        self.snapshots = restored;
        self.pending.clear();
        self.pending_deletions.clear();
        self.deletion_previews.get_mut().clear();
        self.prepared_export = None;
        self.latest_export = None;
        Ok(())
    }
}
