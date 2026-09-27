// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Injected clocks and identifier sources.
//!
//! Nothing in this crate reads a wall clock, a monotonic clock, a random
//! source, or global state. Time arrives as a [`MonotonicMillis`] parameter and
//! identifiers arrive from an [`IdSource`], so every decision this crate makes
//! is a pure function of values the caller supplied
//! (`docs/development/testing-and-delivery.md` section 3.2).
//!
//! Monotonic time is the only time a capability or a lease is allowed to see.
//! A wall clock can move backwards across a suspend, a time-zone change, or a
//! user edit, and an authority that expires on wall time would be replayable by
//! moving it. It is also the protocol's rule: BIP carries no wall-clock
//! timestamps, so a message cannot leak browsing time across a trust boundary.

use bip_types::identity::MonotonicMillis;

/// A reader of browser-owned monotonic time supplied to the isolated core.
///
/// Chromium reads `base::TimeTicks` and passes readings on the ordered service
/// sequence, so this trait exists for the wiring layer and tests rather than
/// for the decision functions themselves.
pub trait MonotonicClock {
    /// The current monotonic reading.
    ///
    /// Successive calls never return a smaller value.
    fn now(&self) -> MonotonicMillis;
}

/// A clock that moves only when a test moves it.
///
/// The default starts at zero. [`Self::advance`] saturates rather than
/// wrapping, because a wrapped clock would run expiry checks backwards.
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
    pub fn advance(&mut self, millis: u64) -> MonotonicMillis {
        self.now = self.now.saturating_add(millis);
        MonotonicMillis(self.now)
    }
}

impl MonotonicClock for ManualClock {
    fn now(&self) -> MonotonicMillis {
        MonotonicMillis(self.now)
    }
}

/// Which namespace an identifier is minted into.
///
/// Identifiers from different namespaces are never interchangeable, and the
/// namespace is part of the request so an implementation cannot accidentally
/// serve a lease identifier where a capability identifier was asked for.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum IdKind {
    /// An actor lease identifier (domain model section 12.3).
    ActorLease,
    /// A capability identifier (domain model section 12.4).
    Capability,
    /// An approval identifier (domain model section 12.5).
    Approval,
}

impl IdKind {
    /// Every namespace, in declaration order.
    pub const ALL: &'static [Self] = &[Self::ActorLease, Self::Capability, Self::Approval];

    /// A short, compiled-in name for the namespace.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ActorLease => "lease",
            Self::Capability => "cap",
            Self::Approval => "approval",
        }
    }
}

/// A source of opaque identifiers.
///
/// The shipping implementation draws unguessable values from the browser
/// process (domain model section 3). Exhaustion returns `None` rather than
/// wrapping: an identifier that could repeat is an identifier that could
/// authorize a replay, so refusing to mint one is the only safe answer.
pub trait IdSource {
    /// Issues the next identifier in `kind`, or `None` once the source is
    /// exhausted.
    fn next_id(&mut self, kind: IdKind) -> Option<String>;
}

/// A deterministic counter-backed identifier source.
///
/// Values look like `lease_0`, `cap_0`, `cap_1`. They are predictable, so this
/// is a test and harness implementation, never the shipping one: domain model
/// section 3 requires unguessable identifiers in the product. It is here
/// because a deterministic test needs a conforming sequence to check itself
/// against.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SequentialIds {
    lease: u64,
    capability: u64,
    approval: u64,
}

impl SequentialIds {
    /// A source whose first identifier in every namespace is numbered zero.
    pub const fn new() -> Self {
        Self {
            lease: 0,
            capability: 0,
            approval: 0,
        }
    }
}

impl IdSource for SequentialIds {
    fn next_id(&mut self, kind: IdKind) -> Option<String> {
        let counter = match kind {
            IdKind::ActorLease => &mut self.lease,
            IdKind::Capability => &mut self.capability,
            IdKind::Approval => &mut self.approval,
        };
        let issued = *counter;
        *counter = counter.checked_add(1)?;
        Some(format!("{}_{issued}", kind.label()))
    }
}

#[cfg(test)]
mod tests {
    use super::{IdKind, IdSource, ManualClock, MonotonicClock, SequentialIds};
    use bip_types::identity::MonotonicMillis;

    #[test]
    fn a_manual_clock_only_moves_when_a_test_moves_it() {
        let mut clock = ManualClock::default();
        assert_eq!(clock.now(), MonotonicMillis(0));
        assert_eq!(clock.advance(250), MonotonicMillis(250));
        assert_eq!(clock.now(), MonotonicMillis(250));
    }

    #[test]
    fn a_manual_clock_saturates_rather_than_running_backwards() {
        let mut clock = ManualClock::at(u64::MAX - 1);
        assert_eq!(clock.advance(10), MonotonicMillis(u64::MAX));
        assert_eq!(clock.advance(10), MonotonicMillis(u64::MAX));
    }

    #[test]
    fn identifier_namespaces_do_not_share_a_counter() {
        let mut ids = SequentialIds::new();
        assert_eq!(ids.next_id(IdKind::ActorLease).as_deref(), Some("lease_0"));
        assert_eq!(ids.next_id(IdKind::Capability).as_deref(), Some("cap_0"));
        assert_eq!(ids.next_id(IdKind::Capability).as_deref(), Some("cap_1"));
        assert_eq!(ids.next_id(IdKind::ActorLease).as_deref(), Some("lease_1"));
        assert_eq!(ids.next_id(IdKind::Approval).as_deref(), Some("approval_0"));
    }

    #[test]
    fn every_namespace_has_a_distinct_compiled_in_prefix() {
        let mut labels: Vec<&str> = IdKind::ALL.iter().map(|kind| kind.label()).collect();
        let count = labels.len();
        labels.sort_unstable();
        labels.dedup();
        assert_eq!(labels.len(), count);
    }
}
