// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Expiry and source Core lifecycle rules for portable restore authority.

use taffy_storage::backup::RestoreSessionPhase;

use super::{BackupRestoreBinding, BackupRestoreProtocol, BackupRestoreProtocolError};

impl BackupRestoreProtocol {
    /// Retires this protocol incarnation. Before commit issuance the retained
    /// plan is withdrawn; after issuance its unknown physical outcome remains
    /// visible for reconciliation, but this disconnected instance can issue
    /// no further decision.
    pub fn service_disconnected(&mut self) -> Result<(), BackupRestoreProtocolError> {
        if !self.connected {
            return Ok(());
        }
        self.connected = false;
        self.last_cancelled_binding = None;
        self.recovery_resolution = None;
        self.recovery_resolution_receipt = None;
        let Some(active) = self.active.as_mut() else {
            return Ok(());
        };
        if active.session.cancel_before_commit().is_ok() {
            self.active = None;
            Ok(())
        } else {
            Err(BackupRestoreProtocolError::ReconcileRequired)
        }
    }

    pub fn active_binding(&self) -> Option<&BackupRestoreBinding> {
        self.active.as_ref().map(|active| &active.binding)
    }

    pub fn phase(&self) -> Option<RestoreSessionPhase> {
        self.active.as_ref().map(|active| active.session.phase())
    }
}
