// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Injected time (testing-and-delivery section 3.2).
//!
//! An audit event carries a wall-clock time, because a person reading their own
//! task history needs to know when something happened. A wall clock can move
//! backwards, so it never decides anything: ordering comes from the per-stream
//! monotonic sequence the journal assigns, and the recorded time is evidence
//! rather than authority.
//!
//! Nothing in this crate reads a clock on its own. Time arrives through
//! [`Clock`], which makes every recorded event a pure function of what the
//! caller supplied.

use core::fmt;

/// A wall-clock reading in milliseconds since the Unix epoch.
///
/// Milliseconds, not nanoseconds: a finer reading is a better fingerprint and
/// no better a record.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct UtcMillis(pub u64);

impl fmt::Display for UtcMillis {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}", self.0)
    }
}

/// A reader of the wall clock.
pub trait Clock {
    /// The current reading.
    fn now_utc(&self) -> UtcMillis;
}

/// A clock that moves only when a test moves it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ManualClock {
    now: u64,
}

impl ManualClock {
    /// A clock reading `millis`.
    pub const fn at(millis: u64) -> Self {
        Self { now: millis }
    }

    /// Moves the clock forward and returns the new reading.
    ///
    /// Saturating: a clock that wrapped would put a later event before an
    /// earlier one.
    pub fn advance(&mut self, millis: u64) -> UtcMillis {
        self.now = self.now.saturating_add(millis);
        UtcMillis(self.now)
    }
}

impl Clock for ManualClock {
    fn now_utc(&self) -> UtcMillis {
        UtcMillis(self.now)
    }
}
