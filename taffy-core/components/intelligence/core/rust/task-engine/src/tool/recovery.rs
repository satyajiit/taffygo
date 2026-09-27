// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a caller may do after a refusal, and the total map from every result
//! code to one of those answers.
//!
//! # There is no unqualified retry, and that is the whole design
//!
//! [`Recovery`] has five members and none of them is "retry". Decision 0054
//! section 4 is explicit about why: a bare retry is the wrong answer to every
//! refusal this product produces. A stale handle needs a fresh observation
//! before anything is retried, a policy denial will deny again, and a budget
//! refusal needs narrowing. Offering "retry" would be offering the one action
//! that is never correct — and a model handed it will take it, because it is
//! the cheapest thing on the list and it looks locally reasonable every single
//! time.
//!
//! Every retry-shaped member therefore states the condition that has to change
//! first, and the condition is part of the name rather than part of a comment
//! somebody may not read.
//!
//! # Two gates, and both have to agree
//!
//! [`recovery_for`] answers what the *result code* permits. It knows nothing
//! about the tool: `IdempotencyClass::recovery_rule` answers what the *tool*
//! permits, and [`permits_unattended_attempt`] is the conjunction. Keeping them
//! apart is what lets each be total over its own vocabulary; taking both
//! together is
//! what makes the product conservative by construction, because either gate
//! alone can stop an attempt and neither can license one.

use bip_types::ActionResultCode;

use super::entry::IdempotencyClass;

/// What a caller may do after a refusal.
///
/// Ordered by strictness in [`Recovery::strictness`] rather than by
/// declaration, because the ordering is a rule and a derived one would change
/// silently when somebody rearranged the members.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Recovery {
    /// Observe the page again, then decide. Nothing is retried against the
    /// world the refusal was produced in.
    ReobserveThenRetry,
    /// Ask for less — a smaller scope, a smaller result, a cheaper route — and
    /// then decide. The answer to a budget refusal.
    NarrowAndRetry,
    /// Stop and wait for the person. The runtime cannot settle this alone.
    AwaitUser,
    /// Do not make this call again. Something else may still be worth trying.
    DoNotRetry,
    /// This line of work is over. Report what is held.
    Abandon,
}

impl Recovery {
    /// Every recovery, from the most permissive to the strictest.
    pub const ALL: &'static [Self] = &[
        Self::ReobserveThenRetry,
        Self::NarrowAndRetry,
        Self::AwaitUser,
        Self::DoNotRetry,
        Self::Abandon,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ReobserveThenRetry => "reobserve_then_retry",
            Self::NarrowAndRetry => "narrow_and_retry",
            Self::AwaitUser => "await_user",
            Self::DoNotRetry => "do_not_retry",
            Self::Abandon => "abandon",
        }
    }

    /// How strict this answer is, as a number only this module's rules read.
    ///
    /// Used to combine two answers about the same refusal without either being
    /// able to loosen the other — see [`Self::stricter_of`].
    pub const fn strictness(self) -> u8 {
        match self {
            Self::ReobserveThenRetry => 1,
            Self::NarrowAndRetry => 2,
            Self::AwaitUser => 3,
            Self::DoNotRetry => 4,
            Self::Abandon => 5,
        }
    }

    /// The stricter of two answers about the same refusal.
    ///
    /// Combining is always a narrowing. The repetition ladder
    /// ([`super::RefusalLedger`]) may make a refusal stricter as it repeats, and
    /// it must never make one *looser*: a policy denial counted a second time
    /// does not become a thing worth observing the page about.
    #[must_use]
    pub const fn stricter_of(self, other: Self) -> Self {
        if other.strictness() > self.strictness() {
            other
        } else {
            self
        }
    }

    /// Whether the runtime may make another attempt on its own initiative.
    ///
    /// True for exactly the two members that name a condition the runtime can
    /// change by itself. `AwaitUser` needs a person; the last two end it.
    pub const fn permits_another_attempt(self) -> bool {
        matches!(self, Self::ReobserveThenRetry | Self::NarrowAndRetry)
    }
}

/// What the result code alone says a caller may do.
///
/// Total over [`ActionResultCode`] by construction: the match has no arm that
/// sweeps up the rest, so a code added to the taxonomy is a compile error here
/// rather than a code that silently inherits somebody else's recovery.
///
/// The grouping follows one rule that is worth reading before disagreeing with
/// an individual arm. **A code that says a side effect may have reached the
/// page never gets an answer the runtime may act on alone.** Those codes carry
/// `SideEffectCertainty::Possible`, meaning the product cannot tell whether the
/// page changed; observing again is not enough, because what is needed is a
/// decision about a change that may already have happened. Whether a *read* may
/// nonetheless be repeated is the tool's question, not the code's, and
/// `IdempotencyClass::recovery_rule` is where it is answered.
pub const fn recovery_for(code: ActionResultCode) -> Recovery {
    match code {
        // Not a refusal at all. There is nothing to recover from, and a caller
        // that asked anyway is told not to repeat a call that succeeded.
        ActionResultCode::Verified
        // Refused by a decision that will be made the same way again. The
        // reason a denial happened can encode facts about the page, the
        // settings or the site table, and none of that belongs in a request
        // body sent to a provider — so the answer is the verdict alone.
        | ActionResultCode::DeniedByPolicy
        | ActionResultCode::ApprovalDenied
        | ActionResultCode::CancelledByUser
        | ActionResultCode::EgressNotAuthorized
        | ActionResultCode::DestinationClassRestricted
        | ActionResultCode::UntrustedContentOrigin
        // A protocol misuse. Repeating it repeats the misuse.
        | ActionResultCode::CommitWithoutPrepare
        // The build does not have it, and will not acquire it in this process.
        | ActionResultCode::Unsupported
        // A side effect may have landed and the runtime cannot tell. It does
        // not get to decide that for itself.
        | ActionResultCode::DispatchFailed
        | ActionResultCode::PostconditionTimeout
        | ActionResultCode::PostconditionFailed
        | ActionResultCode::CancelledByNavigation
        // Not a refusal either: a browser-owned navigation began and its
        // postconditions are still being checked. Issuing the call again would
        // be a second navigation, not a retry of the first.
        | ActionResultCode::NavigationStarted
        // The browser went somewhere other than the address that was asked
        // for - a redirect off the declared destination, or the error document
        // it writes when a host does not answer. Asking again lands in the
        // same place, and nothing here is a decision a person owns: the next
        // move is another address, which is the model's to choose. This code
        // used to await the person, so a site that simply did not resolve ended
        // an errand at a handover on a page nobody could act on (decision
        // 0176).
        | ActionResultCode::DestinationChanged => Recovery::DoNotRetry,

        // The world moved under the handle, the capability or the document. A
        // fresh observation is what makes the next decision about the page
        // that is actually there, and nothing is retried against the old one.
        ActionResultCode::CapabilityExpired
        | ActionResultCode::FrameGone
        | ActionResultCode::DocumentInactive
        | ActionResultCode::StalePageEpoch
        | ActionResultCode::StaleGraph
        | ActionResultCode::NodeGone
        | ActionResultCode::OriginChanged
        | ActionResultCode::RoleOrActionChanged
        | ActionResultCode::NotVisible
        | ActionResultCode::Occluded
        | ActionResultCode::NotEnabled
        | ActionResultCode::NotEditable
        | ActionResultCode::GraphMovedDuringPreflight => Recovery::ReobserveThenRetry,

        // The one refusal that asking for less actually answers.
        ActionResultCode::BudgetExceeded => Recovery::NarrowAndRetry,

        // A person has to decide. Approval is the obvious one; the other two
        // are the cases the product refuses by class or by the prepared-commit
        // rule, where handing back is what remains rather than a fallback.
        ActionResultCode::ApprovalRequired
        | ActionResultCode::SensitiveField
        | ActionResultCode::PreparedEffectChanged
        // The old browser-owned reference cannot be repaired or retried. The
        // person has to supply a fresh value set, which mints new references.
        | ActionResultCode::ValueReferenceUnknown
        // An outcome nobody can report. Reconciliation or a user decision, and
        // never a repeat: this is the code the whole idempotency vocabulary
        // exists for.
        | ActionResultCode::OutcomeUnknown => Recovery::AwaitUser,

        // Whatever was holding this line of work is gone. There is no target
        // to observe again and no narrower version of the same request.
        ActionResultCode::ActorLeaseMissing
        | ActionResultCode::TabGone
        | ActionResultCode::RendererCrashed
        | ActionResultCode::InternalError => Recovery::Abandon,
    }
}

/// Whether the runtime may make another attempt with nobody deciding.
///
/// The conjunction of the two gates, stated once so that no caller has to
/// remember to consult the second. `code` says what happened; `idempotency`
/// comes from the tool's registry row and says what may be repeated.
pub fn permits_unattended_attempt(code: ActionResultCode, idempotency: IdempotencyClass) -> bool {
    recovery_for(code).permits_another_attempt()
        && idempotency.recovery_rule().permits_unattended_retry()
}

#[cfg(test)]
mod tests {
    use super::{permits_unattended_attempt, recovery_for, Recovery};
    use crate::tool::entry::IdempotencyClass;
    use bip_types::result_code::SideEffectCertainty;
    use bip_types::ActionResultCode;

    #[test]
    fn recovery_for_is_total_over_the_taxonomy() {
        // The match itself is what makes this true; the loop is what makes it
        // visible, and it is here so that a code added to the schema shows up
        // as a named failure rather than only as a compile error somebody may
        // settle with a catch-all.
        for code in ActionResultCode::ALL {
            let answer = recovery_for(*code);
            assert!(Recovery::ALL.contains(&answer), "{code:?}");
        }
    }

    #[test]
    fn no_answer_is_an_unqualified_retry() {
        // There is no `Recovery::Retry` to assert against, which is the point.
        // What can be asserted is that the two retry-shaped answers both name
        // the thing that must change first, and that the other three do not
        // permit an attempt at all.
        for recovery in Recovery::ALL {
            let names_a_condition = matches!(
                recovery,
                Recovery::ReobserveThenRetry | Recovery::NarrowAndRetry
            );
            assert_eq!(recovery.permits_another_attempt(), names_a_condition);
        }
    }

    #[test]
    fn a_possible_side_effect_is_never_settled_by_the_runtime_alone() {
        for code in ActionResultCode::ALL {
            if code.side_effect() == SideEffectCertainty::NotPerformed {
                continue;
            }
            assert!(
                !recovery_for(*code).permits_another_attempt(),
                "{code:?} may have reached the page and must not license an attempt"
            );
        }
    }

    #[test]
    fn the_three_named_refusals_get_the_three_answers_the_record_names() {
        // Decision 0054 section 4, by name: a stale handle needs a fresh
        // observation, a policy denial will deny again, a budget refusal needs
        // narrowing.
        assert_eq!(
            recovery_for(ActionResultCode::NodeGone),
            Recovery::ReobserveThenRetry
        );
        assert_eq!(
            recovery_for(ActionResultCode::DeniedByPolicy),
            Recovery::DoNotRetry
        );
        assert_eq!(
            recovery_for(ActionResultCode::BudgetExceeded),
            Recovery::NarrowAndRetry
        );
    }

    #[test]
    fn every_stale_handle_code_asks_for_a_fresh_observation() {
        for code in ActionResultCode::ALL {
            if code.is_stale_handle() {
                assert_eq!(
                    recovery_for(*code),
                    Recovery::ReobserveThenRetry,
                    "{code:?}"
                );
            }
        }
    }

    #[test]
    fn a_landing_the_task_did_not_ask_for_is_the_models_move_not_the_persons() {
        // An address that redirects off its declared destination and an
        // address that does not answer at all reach the core as the same code,
        // and neither is a decision a person owns: the repair is another
        // address. Awaiting the person here ended an errand at a handover on
        // Chromium's own error page (decision 0176).
        assert_eq!(
            recovery_for(ActionResultCode::DestinationChanged),
            Recovery::DoNotRetry
        );
        assert_eq!(
            recovery_for(ActionResultCode::PreparedEffectChanged),
            Recovery::AwaitUser
        );
    }

    #[test]
    fn a_prohibited_field_hands_back_rather_than_tries_something_else() {
        // Taffy never learns to recognise a challenge meant to prove a person
        // is present, so a credential or one-time code field is refused by
        // class. What remains is the person, and this is where that shows up
        // in the recovery vocabulary.
        assert_eq!(
            recovery_for(ActionResultCode::SensitiveField),
            Recovery::AwaitUser
        );
    }

    #[test]
    fn strictness_is_a_distinct_number_per_answer_and_combining_only_narrows() {
        let mut seen: Vec<u8> = Vec::new();
        for recovery in Recovery::ALL {
            assert!(!seen.contains(&recovery.strictness()), "{recovery:?}");
            seen.push(recovery.strictness());
        }
        for left in Recovery::ALL {
            for right in Recovery::ALL {
                let combined = left.stricter_of(*right);
                assert!(combined.strictness() >= left.strictness());
                assert!(combined.strictness() >= right.strictness());
                assert_eq!(combined, right.stricter_of(*left));
            }
        }
    }

    #[test]
    fn both_gates_have_to_agree_before_the_runtime_acts_alone() {
        // A pure read whose handle went stale is the case both gates admit.
        assert!(permits_unattended_attempt(
            ActionResultCode::NodeGone,
            IdempotencyClass::PureRead
        ));
        // The same code on a consequential tool is refused by the tool's gate.
        assert!(!permits_unattended_attempt(
            ActionResultCode::NodeGone,
            IdempotencyClass::Consequential
        ));
        // A pure read whose outcome nobody can report is refused by the code's
        // gate, even though the tool would permit a repeat.
        assert!(!permits_unattended_attempt(
            ActionResultCode::OutcomeUnknown,
            IdempotencyClass::PureRead
        ));
    }
}
