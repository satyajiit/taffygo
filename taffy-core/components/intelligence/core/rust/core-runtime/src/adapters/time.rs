// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Browser-driven wall time shared by deterministic local adapters.

use std::cell::Cell;
use std::rc::Rc;

/// Cloneable clock updated only by the ordered service sequence.
#[derive(Clone, Debug, Default)]
pub struct ServiceClock(Rc<Cell<u64>>);

impl ServiceClock {
    /// Starts at the browser-supplied UTC millisecond reading.
    pub fn at(now_utc_millis: u64) -> Self {
        Self(Rc::new(Cell::new(now_utc_millis)))
    }

    /// Updates the evidence timestamp before one ordered operation.
    pub fn set_utc_millis(&self, now_utc_millis: u64) {
        self.0.set(now_utc_millis);
    }

    /// Current browser-supplied wall time on the ordered service sequence.
    pub fn utc_millis(&self) -> u64 {
        self.0.get()
    }
}

impl task_engine::Clock for ServiceClock {
    fn now_utc(&self) -> task_engine::UtcMillis {
        task_engine::UtcMillis(self.0.get())
    }
}

impl audit_engine::Clock for ServiceClock {
    fn now_utc(&self) -> audit_engine::UtcMillis {
        audit_engine::UtcMillis(self.0.get())
    }
}
