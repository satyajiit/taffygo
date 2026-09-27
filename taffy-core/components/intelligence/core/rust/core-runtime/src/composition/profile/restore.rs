// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Naming why a committed task did not come back at bootstrap.
//!
//! One refused task refuses the whole core, and on 2026-09-23 the phone said
//! so as `[taffy_core_initialization_refused] reason=task-restore label=` —
//! no task and no reason, because the port that opens a task answers a replay
//! it refused with a bare `Rejected`. This is the reason, asked for only once
//! the refusal has already been taken (decision 0235).

use core_service_types as wire;

use super::ProfileServiceRuntime;
use crate::adapters::persistence::decode_task_restore;
use crate::adapters::task::ProductionTaskFactory;

impl ProfileServiceRuntime {
    /// Why one committed task does not restore, in compiled-in words, or
    /// `None` when its records decode and its journal replays.
    ///
    /// `None` means the refusal came from past the replay — the task table,
    /// or a validation of the restored task — and the caller names that
    /// itself. The records are decoded and replayed a second time over this
    /// runtime's own clock and digest, which is what the factory that refused
    /// them was built from, so a deterministic replay gives the same answer
    /// for the reason it gave the first time.
    pub fn task_restore_refusal(&self, record: &wire::TaskRestoreRecord) -> Option<&'static str> {
        match decode_task_restore(record) {
            Err(error) => Some(error.label()),
            Ok(decoded) => ProductionTaskFactory::new(self.clock.clone(), self.digest.clone())
                .replay_refusal(decoded.load),
        }
    }
}
