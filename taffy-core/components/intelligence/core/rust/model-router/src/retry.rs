// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Retry and failover planning, with no clock and no randomness.
//!
//! Retry happens in two layers and they answer different questions. Transport
//! retry asks "did this attempt reach the provider?" and backs off. Semantic
//! retry asks "is this result worth attempting again?" and classifies first —
//! non-retryable classes are checked before retryable ones, so a quota failure
//! never becomes three quota failures.
//!
//! Both layers plan inside the task's budgets. A retry that spends past a
//! budget is a budget that does not exist, so nothing here decides to retry:
//! it decides what a retry would look like, and the ledger decides whether it
//! may happen.
//!
//! Jitter is injected. Real jitter is a random value, and a backoff schedule
//! that cannot be reproduced is a schedule nobody can test.

use crate::defaults;
use crate::request::{ErrorClass, RetryDisposition};

/// Supplies the jitter fraction for one attempt.
///
/// Implementations return a percentage of the maximum jitter to subtract, from
/// `0` for none to `100` for all of it. The host supplies randomness; a test
/// supplies whatever it needs to see.
pub trait Jitter {
    /// The fraction to subtract on `attempt`, as a percentage.
    fn fraction_percent(&self, attempt: u32) -> u32;
}

/// Jitter that subtracts nothing, for tests that care about the schedule.
#[derive(Clone, Copy, Debug, Default)]
pub struct NoJitter;

impl Jitter for NoJitter {
    fn fraction_percent(&self, _attempt: u32) -> u32 {
        0
    }
}

/// The transport backoff delay for `attempt`, counting the first as `0`.
///
/// The base delay doubles up to a cap, then jitter subtracts up to its share.
/// Subtracting rather than adding keeps the cap a real ceiling.
pub fn transport_backoff_millis(attempt: u32, jitter: &dyn Jitter) -> u64 {
    let doubled = defaults::TRANSPORT_BACKOFF_BASE_MILLIS
        .checked_shl(attempt.min(32))
        .unwrap_or(defaults::TRANSPORT_BACKOFF_CAP_MILLIS);
    let delay = doubled.min(defaults::TRANSPORT_BACKOFF_CAP_MILLIS);
    let share = u64::from(defaults::TRANSPORT_BACKOFF_JITTER_PERCENT);
    let fraction = u64::from(jitter.fraction_percent(attempt).min(100));
    let subtract = delay.saturating_mul(share).saturating_mul(fraction) / 10_000;
    delay.saturating_sub(subtract)
}

/// The semantic retry delay for `attempt`, or `None` once attempts run out.
pub fn semantic_backoff_millis(attempt: u32) -> Option<u64> {
    if attempt + 1 >= defaults::SEMANTIC_RETRY_ATTEMPTS {
        return None;
    }
    Some(
        defaults::SEMANTIC_RETRY_BASE_MILLIS
            .checked_shl(attempt.min(32))
            .unwrap_or(defaults::SEMANTIC_RETRY_BASE_MILLIS),
    )
}

/// What to do about a delay the server asked for.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ServerDelayVerdict {
    /// Wait this long, then retry.
    Wait {
        /// Milliseconds to wait.
        millis: u64,
    },
    /// The server asked for longer than is worth waiting; fail now.
    ///
    /// Waiting silently on a server's own hint is how a slow provider becomes
    /// an unresponsive product.
    FailFast {
        /// What the server asked for.
        requested_millis: u64,
    },
}

/// Reads a server-requested delay against the cap.
pub fn honor_server_delay(requested_millis: u64) -> ServerDelayVerdict {
    if requested_millis > defaults::SERVER_RETRY_DELAY_CAP_MILLIS {
        ServerDelayVerdict::FailFast { requested_millis }
    } else {
        ServerDelayVerdict::Wait {
            millis: requested_millis,
        }
    }
}

/// What should happen after one failed attempt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AttemptVerdict {
    /// Retry the same request after this delay.
    RetryAfter {
        /// Milliseconds to wait.
        millis: u64,
    },
    /// Try the next candidate in the plan.
    Failover,
    /// Stop.
    Stop,
}

/// Decides what one failed attempt earns.
///
/// The order is the contract: classification first, then the server's own hint,
/// then the schedule. A class that cannot be retried is answered before any
/// delay is computed, so an unretryable failure never waits.
pub fn verdict(
    class: ErrorClass,
    attempt: u32,
    retry_after_millis: Option<u64>,
    has_failover_candidate: bool,
    jitter: &dyn Jitter,
) -> AttemptVerdict {
    if class.disposition() == RetryDisposition::Terminal {
        return if has_failover_candidate && class.permits_failover() {
            AttemptVerdict::Failover
        } else {
            AttemptVerdict::Stop
        };
    }
    if let Some(requested) = retry_after_millis {
        return match honor_server_delay(requested) {
            ServerDelayVerdict::Wait { millis } => AttemptVerdict::RetryAfter { millis },
            ServerDelayVerdict::FailFast { .. } => {
                if has_failover_candidate {
                    AttemptVerdict::Failover
                } else {
                    AttemptVerdict::Stop
                }
            }
        };
    }
    // The attempt ceiling is shared by retries and substitutes. Once another
    // same-candidate retry would consume the final slot, prefer the immutable
    // substitute the original route plan already disclosed. Without this
    // branch a retryable overload could spend every slot on candidate zero
    // and make a non-empty failover plan unreachable.
    if has_failover_candidate && attempt.saturating_add(2) >= defaults::SEMANTIC_RETRY_ATTEMPTS {
        return AttemptVerdict::Failover;
    }
    if semantic_backoff_millis(attempt).is_none() {
        return if has_failover_candidate {
            AttemptVerdict::Failover
        } else {
            AttemptVerdict::Stop
        };
    }
    AttemptVerdict::RetryAfter {
        millis: transport_backoff_millis(attempt, jitter),
    }
}
