// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The delay between one failed transfer and the next attempt.
//!
//! Jitter is injected for the same reason it is in the model router: real
//! jitter is a random value, and a schedule that cannot be reproduced is a
//! schedule nobody can test. Jitter subtracts rather than adds, so the cap is a
//! real ceiling and not a value the schedule occasionally exceeds.

use crate::defaults;

/// Supplies the jitter fraction for one attempt.
pub trait Jitter {
    /// The share of the maximum jitter to subtract on `attempt`, as a
    /// percentage from `0` to `100`.
    fn fraction_percent(&self, attempt: u32) -> u32;
}

/// Jitter that subtracts nothing, for a test that cares about the schedule.
#[derive(Clone, Copy, Debug, Default)]
pub struct NoJitter;

impl Jitter for NoJitter {
    fn fraction_percent(&self, _attempt: u32) -> u32 {
        0
    }
}

/// The delay after failed attempt index `attempt`, counting the first as `0`.
///
/// `None` once the attempts are used up, which is how the caller learns to
/// stop rather than by comparing a count itself.
pub fn delay_millis(attempt: u32, jitter: &dyn Jitter) -> Option<u64> {
    if attempt >= defaults::MAX_TRANSFER_ATTEMPTS.saturating_sub(1) {
        return None;
    }
    let doubled = defaults::BACKOFF_BASE_MILLIS
        .checked_shl(attempt.min(32))
        .unwrap_or(defaults::BACKOFF_CAP_MILLIS);
    let delay = doubled.min(defaults::BACKOFF_CAP_MILLIS);
    let share = u64::from(defaults::BACKOFF_JITTER_PERCENT);
    let fraction = u64::from(jitter.fraction_percent(attempt).min(100));
    let subtract = delay.saturating_mul(share).saturating_mul(fraction) / 10_000;
    Some(delay.saturating_sub(subtract))
}

#[cfg(test)]
mod tests {
    use super::{delay_millis, Jitter, NoJitter};
    use crate::defaults;

    struct FullJitter;

    impl Jitter for FullJitter {
        fn fraction_percent(&self, _attempt: u32) -> u32 {
            100
        }
    }

    #[test]
    fn the_schedule_doubles_then_holds_at_the_cap() {
        let first = delay_millis(0, &NoJitter);
        let second = delay_millis(1, &NoJitter);
        assert_eq!(first, Some(defaults::BACKOFF_BASE_MILLIS));
        assert_eq!(second, Some(defaults::BACKOFF_BASE_MILLIS * 2));
        for attempt in 0..defaults::MAX_TRANSFER_ATTEMPTS.saturating_sub(1) {
            let delay = delay_millis(attempt, &NoJitter).unwrap_or_else(|| unreachable!());
            assert!(delay <= defaults::BACKOFF_CAP_MILLIS);
        }
    }

    #[test]
    fn attempts_run_out_rather_than_wrapping() {
        assert_eq!(
            delay_millis(defaults::MAX_TRANSFER_ATTEMPTS.saturating_sub(1), &NoJitter),
            None
        );
        assert_eq!(
            delay_millis(defaults::MAX_TRANSFER_ATTEMPTS, &NoJitter),
            None
        );
        assert_eq!(delay_millis(u32::MAX, &NoJitter), None);
    }

    #[test]
    fn jitter_only_ever_subtracts() {
        for attempt in 0..defaults::MAX_TRANSFER_ATTEMPTS.saturating_sub(1) {
            let plain = delay_millis(attempt, &NoJitter).unwrap_or_else(|| unreachable!());
            let jittered = delay_millis(attempt, &FullJitter).unwrap_or_else(|| unreachable!());
            assert!(jittered <= plain, "attempt {attempt}");
            let floor = plain - plain * u64::from(defaults::BACKOFF_JITTER_PERCENT) / 100;
            assert!(jittered >= floor, "attempt {attempt}");
        }
    }
}
