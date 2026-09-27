// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic time and trace inputs owned by the reducer boundary.
//!
//! These values are deliberately independent of the audit implementation.
//! The runtime maps them into audit records after a transition becomes
//! durable; the task engine only needs an injected reading and an opaque trace.

use core::fmt;

/// A wall-clock reading in milliseconds since the Unix epoch.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct UtcMillis(pub u64);

impl fmt::Display for UtcMillis {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}", self.0)
    }
}

/// A reader of the wall clock supplied by the ordered core sequence.
pub trait Clock {
    /// The current reading.
    fn now_utc(&self) -> UtcMillis;
}

/// A clock that moves only when a deterministic test or host moves it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ManualClock {
    now: u64,
}

impl ManualClock {
    /// A clock reading `millis`.
    pub const fn at(millis: u64) -> Self {
        Self { now: millis }
    }

    /// Moves the clock forward with saturation.
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

/// Opaque trace identifier for one unit of work.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct TraceId(String);

impl TraceId {
    /// Wraps a core-service supplied trace identifier.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value, for correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for TraceId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}
