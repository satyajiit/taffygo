// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a transfer's ending does to an asset's state.
//!
//! One function, and the reason it is one function is that every way a transfer
//! can end has to reach exactly one of four places: more bytes wanted, bytes to
//! judge, a wait, or a refusal. Spreading that across the call sites is how a
//! transfer ends up in two states at once.

use super::backoff::{delay_millis, Jitter};
use crate::defaults;
use crate::digest::IntegrityVerdict;
use crate::state::{AssetState, Presence, RefusalReason};

/// How a transfer ended.
///
/// There is no "still going" member. Byte-level progress never reaches the
/// core: it is a fact the browser already has, the surface reads it from the
/// browser's own projection, and sending it here would be two process hops per
/// tick to update a number nothing in the core decides anything with. What the
/// core is told is how a transfer *ended*, which is what changes a state.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TransferOutcome {
    /// The transfer stopped before the end, and what is on disk may be resumed.
    Interrupted {
        /// Total bytes on disk.
        written_bytes: u64,
    },
    /// The origin answered with something other than the bytes.
    ///
    /// `permanent` separates "this artifact is not there" from "ask again
    /// later": a row the origin does not have will not appear because the
    /// device waited, and retrying it spends a person's data for nothing.
    OriginRefused {
        /// Whether asking again cannot help.
        permanent: bool,
    },
    /// Every byte arrived and was judged.
    Judged(IntegrityVerdict),
    /// The bytes were judged sound and committed to their final place.
    Installed,
    /// A person turned the asset off.
    Declined,
}

/// The state after `outcome`, given the state before it.
///
/// `now_monotonic_ms` and `jitter` are the caller's, so the same inputs always
/// produce the same state — which is what lets a recorded install be replayed
/// and what lets a test assert a schedule rather than observe one.
/// An outcome that cannot follow `before` is a stale or ill-ordered report and
/// leaves the state unchanged. The browser verifies and commits in one
/// operation, so `Installed` may legitimately follow an absent or partial
/// state; once installed or declined, late transfer reports cannot regress it.
pub fn advance(
    before: &AssetState,
    outcome: TransferOutcome,
    now_monotonic_ms: u64,
    jitter: &dyn Jitter,
) -> AssetState {
    if !accepts(before, outcome) {
        return before.clone();
    }

    let mut after = before.clone();
    match outcome {
        TransferOutcome::Interrupted { written_bytes } => {
            if defaults::KEEP_PARTIAL_TRANSFERS {
                after.progress.written_bytes = written_bytes.min(after.progress.total_bytes);
                after.presence = if after.progress.written_bytes > 0 {
                    Presence::Partial
                } else {
                    Presence::Absent
                };
            } else {
                after.progress.written_bytes = 0;
                after.presence = Presence::Absent;
            }
            schedule_retry(&mut after, now_monotonic_ms, jitter);
        }
        TransferOutcome::OriginRefused { permanent } => {
            if permanent {
                after.attempts = defaults::MAX_TRANSFER_ATTEMPTS;
                after.retry_after_monotonic_ms = 0;
                after.refusal = Some(RefusalReason::AttemptsExhausted);
            } else {
                schedule_retry(&mut after, now_monotonic_ms, jitter);
            }
        }
        TransferOutcome::Judged(verdict) => {
            if verdict.is_sound() {
                after.presence = Presence::Complete;
                after.progress.written_bytes = after.progress.total_bytes;
                after.attempts = 0;
                after.retry_after_monotonic_ms = 0;
                after.refusal = None;
            } else {
                // Bytes that failed a digest are not bytes to resume from. The
                // next attempt starts over, because a partial file that hashed
                // wrong is a partial file whose earlier half may be wrong too.
                after.progress.written_bytes = 0;
                after.presence = Presence::Absent;
                after.refusal = Some(RefusalReason::IntegrityFailed);
                schedule_retry(&mut after, now_monotonic_ms, jitter);
            }
        }
        TransferOutcome::Installed => {
            after.presence = Presence::Installed;
            after.progress.written_bytes = after.progress.total_bytes;
            after.attempts = 0;
            after.refusal = None;
            after.retry_after_monotonic_ms = 0;
        }
        TransferOutcome::Declined => {
            after.refusal = Some(RefusalReason::DeclinedByPerson);
            after.presence = Presence::Absent;
            after.progress.written_bytes = 0;
            after.attempts = 0;
            after.retry_after_monotonic_ms = 0;
        }
    }
    after
}

/// Starts a fresh bounded failure series for a newly admitted explicit retry.
///
/// Planning is pure and cannot infer that a repeated call is a new request.
/// The caller applies this transition exactly once on that request edge, then
/// persists the returned state before asking [`super::plan_one_asset`] again.
/// It preserves staged bytes so the new series can resume them. Installed and
/// non-retryable states are unchanged.
pub fn restart_retry_series(before: &AssetState) -> AssetState {
    let may_restart = before.refusal.is_some_and(RefusalReason::is_retryable)
        || before.attempts >= defaults::MAX_TRANSFER_ATTEMPTS;
    if before.is_installed()
        || before.refusal.is_some_and(|reason| !reason.is_retryable())
        || !may_restart
    {
        return before.clone();
    }

    let mut after = before.clone();
    after.attempts = 0;
    after.retry_after_monotonic_ms = 0;
    after.refusal = None;
    after
}

/// Whether an ending can follow the fact currently recorded for the asset.
///
/// Reports may be duplicated or arrive after a terminal report. Ignoring one
/// that cannot follow `before` preserves the last usable fact without turning
/// a stale browser message into an install or a fresh retry series.
fn accepts(before: &AssetState, outcome: TransferOutcome) -> bool {
    if before.refusal == Some(RefusalReason::DeclinedByPerson) {
        return matches!(outcome, TransferOutcome::Declined);
    }

    !matches!(
        (before.presence, outcome),
        (
            Presence::Installed,
            TransferOutcome::Interrupted { .. }
                | TransferOutcome::OriginRefused { .. }
                | TransferOutcome::Judged(_)
        )
    )
}

/// Counts the attempt and sets when the next one may start.
///
/// The reason the last attempt failed is left alone. It is what a surface says
/// while the device waits — "could not verify the file, trying again shortly" —
/// and clearing it would leave a person watching a progress bar that stopped
/// for no stated reason. What ends the waiting is the attempt count, not the
/// reason, so a retryable reason and a scheduled retry coexist by design.
fn schedule_retry(state: &mut AssetState, now_monotonic_ms: u64, jitter: &dyn Jitter) {
    state.attempts = state.attempts.saturating_add(1);
    if state.attempts >= defaults::MAX_TRANSFER_ATTEMPTS {
        state.retry_after_monotonic_ms = 0;
        state.refusal = Some(RefusalReason::AttemptsExhausted);
        return;
    }
    // `attempts` counts completed transfers, while `delay_millis` is indexed
    // from zero. The first failure therefore uses the base delay, not the
    // second rung of the schedule.
    let failed_attempt_index = state.attempts.saturating_sub(1);
    if let Some(delay) = delay_millis(failed_attempt_index, jitter) {
        state.retry_after_monotonic_ms = now_monotonic_ms.saturating_add(delay);
    } else {
        state.retry_after_monotonic_ms = 0;
        state.refusal = Some(RefusalReason::AttemptsExhausted);
    }
}

#[cfg(test)]
mod tests {
    use super::{advance, restart_retry_series, TransferOutcome};
    use crate::defaults;
    use crate::digest::IntegrityVerdict;
    use crate::ids::{AssetId, AssetRevision};
    use crate::plan::backoff::NoJitter;
    use crate::state::{AssetState, Presence, RefusalReason};

    fn absent() -> AssetState {
        AssetState::absent(
            AssetId::parse("python-stdlib").unwrap_or_else(|_| unreachable!()),
            AssetRevision::parse("3.14.2").unwrap_or_else(|_| unreachable!()),
            1_000,
        )
    }

    #[test]
    fn an_interruption_keeps_its_bytes_and_schedules_a_retry() {
        let after = advance(
            &absent(),
            TransferOutcome::Interrupted { written_bytes: 400 },
            10_000,
            &NoJitter,
        );
        assert_eq!(after.presence, Presence::Partial);
        assert_eq!(after.progress.written_bytes, 400);
        assert_eq!(after.resume_offset(), 400);
        assert_eq!(after.attempts, 1);
        assert!(after.retry_after_monotonic_ms > 10_000);
    }

    #[test]
    fn the_first_failed_transfer_waits_for_the_configured_base_delay() {
        let after = advance(
            &absent(),
            TransferOutcome::Interrupted { written_bytes: 0 },
            10_000,
            &NoJitter,
        );
        assert_eq!(
            after.retry_after_monotonic_ms,
            10_000 + defaults::BACKOFF_BASE_MILLIS
        );
    }

    #[test]
    fn sound_bytes_are_complete_and_not_yet_installed() {
        let mut before = absent();
        before.presence = Presence::Partial;
        before.progress.written_bytes = 400;
        before.attempts = 2;
        before.retry_after_monotonic_ms = 99;
        before.refusal = Some(RefusalReason::IntegrityFailed);
        let after = advance(
            &before,
            TransferOutcome::Judged(IntegrityVerdict::Sound),
            0,
            &NoJitter,
        );
        assert_eq!(after.presence, Presence::Complete);
        assert_eq!(after.progress.written_bytes, 1_000);
        assert_eq!(after.attempts, 0);
        assert_eq!(after.retry_after_monotonic_ms, 0);
        assert_eq!(after.refusal, None);
        assert!(!after.is_installed(), "verified is not installed");
    }

    #[test]
    fn a_wrong_digest_discards_the_partial_rather_than_resuming_it() {
        let mut before = absent();
        before.presence = Presence::Partial;
        before.progress.written_bytes = 400;
        let after = advance(
            &before,
            TransferOutcome::Judged(IntegrityVerdict::WrongDigest),
            0,
            &NoJitter,
        );
        assert_eq!(after.presence, Presence::Absent);
        assert_eq!(after.resume_offset(), 0);
        assert_eq!(after.refusal, Some(RefusalReason::IntegrityFailed));
    }

    #[test]
    fn a_permanent_origin_refusal_does_not_burn_five_attempts_first() {
        let after = advance(
            &absent(),
            TransferOutcome::OriginRefused { permanent: true },
            0,
            &NoJitter,
        );
        assert_eq!(after.attempts, defaults::MAX_TRANSFER_ATTEMPTS);
        assert_eq!(after.refusal, Some(RefusalReason::AttemptsExhausted));
    }

    #[test]
    fn attempts_run_out_and_stay_out() {
        let mut state = absent();
        for _ in 0..defaults::MAX_TRANSFER_ATTEMPTS {
            state = advance(
                &state,
                TransferOutcome::OriginRefused { permanent: false },
                0,
                &NoJitter,
            );
        }
        assert_eq!(state.refusal, Some(RefusalReason::AttemptsExhausted));
        assert_eq!(state.retry_after_monotonic_ms, 0);
    }

    #[test]
    fn a_fresh_request_starts_a_new_failure_series() {
        let mut before = absent();
        before.presence = Presence::Partial;
        before.progress.written_bytes = 100;
        before.attempts = defaults::MAX_TRANSFER_ATTEMPTS;
        before.refusal = Some(RefusalReason::AttemptsExhausted);
        let restarted = restart_retry_series(&before);
        assert_eq!(restarted.attempts, 0);
        assert_eq!(restarted.refusal, None);
        assert_eq!(restarted.resume_offset(), 100);
        let after = advance(
            &restarted,
            TransferOutcome::Interrupted { written_bytes: 100 },
            10_000,
            &NoJitter,
        );
        assert_eq!(after.attempts, 1);
        assert_eq!(after.refusal, None);
        assert_eq!(
            after.retry_after_monotonic_ms,
            10_000 + defaults::BACKOFF_BASE_MILLIS
        );
    }

    #[test]
    fn installing_clears_the_retry_and_fills_the_progress() {
        let mut before = absent();
        before.attempts = 2;
        before.retry_after_monotonic_ms = 99;
        let after = advance(&before, TransferOutcome::Installed, 0, &NoJitter);
        assert!(after.is_installed());
        assert_eq!(after.progress.basis_points(), 10_000);
        assert_eq!(after.attempts, 0);
        assert_eq!(after.retry_after_monotonic_ms, 0);
        assert_eq!(after.refusal, None);
    }

    #[test]
    fn late_transfer_results_cannot_regress_terminal_states() {
        let installed = advance(&absent(), TransferOutcome::Installed, 0, &NoJitter);
        let declined = advance(&installed, TransferOutcome::Declined, 0, &NoJitter);

        for before in [&installed, &declined] {
            for outcome in [
                TransferOutcome::Interrupted { written_bytes: 400 },
                TransferOutcome::OriginRefused { permanent: false },
                TransferOutcome::Judged(IntegrityVerdict::WrongDigest),
            ] {
                let after = advance(before, outcome, 10_000, &NoJitter);
                assert_eq!(
                    &after, before,
                    "{:?} followed by {outcome:?}",
                    before.presence
                );
            }
        }
    }

    #[test]
    fn a_person_declining_is_not_retryable() {
        let after = advance(&absent(), TransferOutcome::Declined, 0, &NoJitter);
        assert_eq!(after.refusal, Some(RefusalReason::DeclinedByPerson));
        assert!(!RefusalReason::DeclinedByPerson.is_retryable());
        assert_eq!(restart_retry_series(&after), after);
    }
}
