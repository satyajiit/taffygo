// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the browser broker reports back at each effect step (protocol
//! specification sections 11.6 and 12, steps seven to ten).
//!
//! The decision steps of the sequence read observed values. The effect steps
//! read outcomes, and an outcome is the one thing this crate cannot compute for
//! itself: whether a journal write landed, whether the input path accepted the
//! command, what the verifier saw, and whether the ledger spent the authority.
//! Every one of them arrives as a closed enumeration, so the sequence can be
//! driven as a table and nothing has to be inferred from a string.
//!
//! # Renderer acknowledgement is not verification
//!
//! Section 11.6 is explicit: a renderer saying it did the thing yields
//! `DISPATCHED`, never `VERIFIED`. [`summarize_postconditions`] is that sentence
//! as a function — a report where every declared effect is satisfied but only
//! the renderer says so becomes [`PostconditionReport::AcknowledgedOnly`], and
//! the sequence keeps waiting for browser-owned corroboration rather than
//! ending in success.

use bip_types::action::{PostconditionOutcome, VerifierKind};

/// Whether the dispatching intent reached the task journal (step 7).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum JournalOutcome {
    /// The intent is durable. The effect may now be attempted.
    Recorded,
    /// The write failed. Nothing may be dispatched, because a side effect
    /// nobody recorded the intent for cannot be reconciled after a crash.
    WriteFailed,
    /// The broker chose not to record an intent, so nothing was sent. The
    /// authority is still spent — it was put in flight and is never returned to
    /// the issued state — but the page was not touched.
    NotAttempted,
}

impl JournalOutcome {
    /// Every outcome, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Recorded, Self::WriteFailed, Self::NotAttempted];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Recorded => "recorded",
            Self::WriteFailed => "write_failed",
            Self::NotAttempted => "not_attempted",
        }
    }
}

/// What the browser and renderer input path did with the command (step 8).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DispatchAck {
    /// The executor accepted the command. The postconditions are now checked.
    AcceptedByExecutor,
    /// The executor refused it. Nothing reached the page.
    RejectedByExecutor,
    /// The renderer went away between the checks and the send.
    RendererGone,
    /// The target stopped resolving between the checks and the send.
    TargetGoneAtDispatch,
    /// The user took over, or cancelled, before the command was sent.
    CancelledByUserBeforeSend,
    /// A browser-owned navigation committed before the command was sent.
    NavigationCommittedBeforeSend,
}

impl DispatchAck {
    /// Every acknowledgement, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::AcceptedByExecutor,
        Self::RejectedByExecutor,
        Self::RendererGone,
        Self::TargetGoneAtDispatch,
        Self::CancelledByUserBeforeSend,
        Self::NavigationCommittedBeforeSend,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::AcceptedByExecutor => "accepted_by_executor",
            Self::RejectedByExecutor => "rejected_by_executor",
            Self::RendererGone => "renderer_gone",
            Self::TargetGoneAtDispatch => "target_gone_at_dispatch",
            Self::CancelledByUserBeforeSend => "cancelled_by_user_before_send",
            Self::NavigationCommittedBeforeSend => "navigation_committed_before_send",
        }
    }
}

/// What the postcondition verifier saw (step 9).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PostconditionReport {
    /// Every declared effect held, and a browser-owned event or a fresh
    /// observation corroborated each one.
    Satisfied,
    /// Every declared effect held, but only the renderer says so. This is a
    /// dispatch, not a verification, and the sequence keeps waiting.
    AcknowledgedOnly,
    /// At least one declared effect did not hold.
    Contradicted,
    /// The deadline passed with nothing conclusive observed.
    DeadlineExpired,
    /// A browser-owned navigation ended the observation.
    NavigationCommitted,
    /// The user took over or cancelled after the command was sent.
    CancelledByUserAfterSend,
    /// The renderer crashed while the effect was being checked.
    RendererCrashed,
    /// Nothing conclusive was reported and the outcome cannot be reconciled.
    Unreconciled,
}

impl PostconditionReport {
    /// Every report, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Satisfied,
        Self::AcknowledgedOnly,
        Self::Contradicted,
        Self::DeadlineExpired,
        Self::NavigationCommitted,
        Self::CancelledByUserAfterSend,
        Self::RendererCrashed,
        Self::Unreconciled,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Satisfied => "satisfied",
            Self::AcknowledgedOnly => "acknowledged_only",
            Self::Contradicted => "contradicted",
            Self::DeadlineExpired => "deadline_expired",
            Self::NavigationCommitted => "navigation_committed",
            Self::CancelledByUserAfterSend => "cancelled_by_user_after_send",
            Self::RendererCrashed => "renderer_crashed",
            Self::Unreconciled => "unreconciled",
        }
    }
}

/// What the capability ledger did when the terminal result was recorded
/// (step 10).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ConsumptionOutcome {
    /// The ledger spent the authority now.
    Spent,
    /// There was no live authority to spend, because the sequence was refused
    /// before any was put in flight.
    NothingToSpend,
    /// The authority had already been spent. Either a replay reached the ledger
    /// or one capability authorized two dispatches; both are broker faults.
    AlreadySpent,
}

impl ConsumptionOutcome {
    /// Every outcome, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Spent, Self::NothingToSpend, Self::AlreadySpent];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Spent => "spent",
            Self::NothingToSpend => "nothing_to_spend",
            Self::AlreadySpent => "already_spent",
        }
    }
}

/// Whether the browser broker, rather than the renderer, saw the effect.
///
/// Only a browser-owned navigation or tab event, a network event, a fresh
/// observation, or a delta observation corroborates a postcondition. A renderer
/// acknowledgement is the renderer's own word about its own behaviour, and a
/// compromised renderer may lie (specification section 11.4).
///
/// The network event is browser-owned for the same reason and more sharply: it
/// is the only witness to a request that was never supposed to happen, and the
/// component it would exonerate is the one that would have made it.
pub const fn is_browser_owned(verifier: VerifierKind) -> bool {
    match verifier {
        VerifierKind::BrowserNavigationEvent
        | VerifierKind::BrowserTabEvent
        | VerifierKind::BrowserNetworkEvent
        | VerifierKind::FreshSnapshot
        | VerifierKind::DeltaObservation => true,
        VerifierKind::RendererAcknowledgement => false,
    }
}

/// Folds the verifier's per-postcondition outcomes into one report.
///
/// Three rules, in this order:
///
/// - **Nothing observed is not success.** An empty list after a dispatch means
///   the verifier reported nothing at all, so the outcome cannot be reconciled.
/// - **One contradiction contradicts the proposal.** A proposal declares every
///   effect it expects, so a single unsatisfied effect refuses the whole thing.
/// - **Every satisfied effect needs browser-owned corroboration.** One effect
///   whose only witness is the renderer keeps the whole report at
///   [`PostconditionReport::AcknowledgedOnly`].
pub fn summarize_postconditions(outcomes: &[PostconditionOutcome]) -> PostconditionReport {
    if outcomes.is_empty() {
        return PostconditionReport::Unreconciled;
    }
    if outcomes.iter().any(|outcome| !outcome.satisfied) {
        return PostconditionReport::Contradicted;
    }
    if outcomes
        .iter()
        .all(|outcome| is_browser_owned(outcome.verifier))
    {
        PostconditionReport::Satisfied
    } else {
        PostconditionReport::AcknowledgedOnly
    }
}

#[cfg(test)]
mod tests {
    use super::{
        is_browser_owned, summarize_postconditions, ConsumptionOutcome, DispatchAck,
        JournalOutcome, PostconditionReport,
    };
    use bip_types::action::{Postcondition, PostconditionKind, PostconditionOutcome, VerifierKind};

    fn outcome(satisfied: bool, verifier: VerifierKind) -> PostconditionOutcome {
        PostconditionOutcome {
            postcondition: Postcondition {
                kind: PostconditionKind::CommittedNavigation,
                expected_destination: None,
                allowed_origins: None,
                expected_node_state: None,
                timeout_ms: None,
            },
            satisfied,
            verifier,
            observed_graph_revision: None,
            detail_code: None,
        }
    }

    #[test]
    fn a_verifier_that_reported_nothing_is_not_a_verification() {
        assert_eq!(
            summarize_postconditions(&[]),
            PostconditionReport::Unreconciled
        );
    }

    #[test]
    fn one_unsatisfied_effect_contradicts_the_whole_proposal() {
        let outcomes = [
            outcome(true, VerifierKind::BrowserNavigationEvent),
            outcome(false, VerifierKind::FreshSnapshot),
        ];
        assert_eq!(
            summarize_postconditions(&outcomes),
            PostconditionReport::Contradicted
        );
    }

    #[test]
    fn a_renderer_acknowledgement_alone_is_a_dispatch_and_not_a_verification() {
        let outcomes = [outcome(true, VerifierKind::RendererAcknowledgement)];
        assert_eq!(
            summarize_postconditions(&outcomes),
            PostconditionReport::AcknowledgedOnly
        );

        let mixed = [
            outcome(true, VerifierKind::BrowserTabEvent),
            outcome(true, VerifierKind::RendererAcknowledgement),
        ];
        assert_eq!(
            summarize_postconditions(&mixed),
            PostconditionReport::AcknowledgedOnly
        );
    }

    #[test]
    fn every_effect_corroborated_by_the_browser_verifies() {
        for verifier in VerifierKind::ALL {
            let outcomes = [outcome(true, *verifier)];
            let expected = if is_browser_owned(*verifier) {
                PostconditionReport::Satisfied
            } else {
                PostconditionReport::AcknowledgedOnly
            };
            assert_eq!(summarize_postconditions(&outcomes), expected);
        }
    }

    #[test]
    fn every_reported_outcome_has_a_distinct_compiled_in_name() {
        let mut labels: Vec<&str> = JournalOutcome::ALL
            .iter()
            .map(|value| value.label())
            .chain(DispatchAck::ALL.iter().map(|value| value.label()))
            .chain(PostconditionReport::ALL.iter().map(|value| value.label()))
            .chain(ConsumptionOutcome::ALL.iter().map(|value| value.label()))
            .collect();
        let count = labels.len();
        labels.sort_unstable();
        labels.dedup();
        assert_eq!(labels.len(), count);
    }
}
