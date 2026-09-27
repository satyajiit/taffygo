// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a tool call produced: something, nothing, or a refusal.
//!
//! # Why "nothing" is not one shape
//!
//! `PageArena::readability` in `core-runtime` already makes this split, and it
//! makes it for the reason that matters here too. A page that held nothing and
//! a page whose content this build was not allowed to see look identical at the
//! call site — both come back with zero items — and a consumer handed the
//! second as if it were the first reports that there was nothing there. That
//! sentence then travels: into a plan, into a result, into an artifact a person
//! reads, and by then the fact that a bound was spent or a classification
//! withheld everything is not recoverable from anything downstream.
//!
//! So [`ToolOutcome::Ok`] cannot exist with nothing in it — the count is a
//! `NonZeroU32` and the type refuses the construction — and every empty result
//! has to name an [`EmptyReason`]. Exactly one of those reasons is evidence
//! that nothing was there, and [`EmptyReason::is_evidence_of_absence`] is the
//! only place a caller may ask.

use core::num::NonZeroU32;

use bip_types::ActionResultCode;

use super::recovery::{recovery_for, Recovery};

/// Why a tool produced nothing.
///
/// Closed, and deliberately not orderable or defaultable: picking the "safest"
/// reason automatically is how a bound spent quietly becomes a page that was
/// empty.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum EmptyReason {
    /// The tool looked at everything in scope and nothing matched. The only
    /// reason that is evidence about the world.
    NothingMatched,
    /// The task's source scope removed every candidate before the tool looked.
    /// Says nothing about what the excluded sources held.
    ScopeExcludedEverything,
    /// Nothing has been observed yet, so there was nothing to look at.
    NotObserved,
    /// Everything found was withheld by classification. Says the opposite of
    /// absence: something was there.
    WithheldByClassification,
    /// A bound — an arena, a budget, a ceiling — was reached before anything
    /// crossed.
    BoundSpent,
}

impl EmptyReason {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::NothingMatched,
        Self::ScopeExcludedEverything,
        Self::NotObserved,
        Self::WithheldByClassification,
        Self::BoundSpent,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::NothingMatched => "nothing_matched",
            Self::ScopeExcludedEverything => "scope_excluded_everything",
            Self::NotObserved => "not_observed",
            Self::WithheldByClassification => "withheld_by_classification",
            Self::BoundSpent => "bound_spent",
        }
    }

    /// Whether this emptiness may travel on as evidence that nothing was there.
    ///
    /// True for exactly one member. The other four are all cases where
    /// something may have been present and this call could not see it, and the
    /// distance between "we found none" and "we could not look" is the whole
    /// reason the enumeration exists.
    pub const fn is_evidence_of_absence(self) -> bool {
        matches!(self, Self::NothingMatched)
    }
}

/// What one tool call produced.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ToolOutcome {
    /// The call produced something, and how much.
    ///
    /// The count cannot be zero. A caller with nothing to report has to choose
    /// an [`EmptyReason`], which is the whole point: stating why it is empty is
    /// a decision, and the type makes it one that has to be made.
    Ok {
        /// How many items came back.
        items: NonZeroU32,
    },
    /// The call ran and produced nothing, for a stated reason.
    Empty(EmptyReason),
    /// The call was refused.
    Refused {
        /// The protocol code that says why.
        code: ActionResultCode,
        /// What the caller may do next. Never an unqualified retry — see
        /// [`Recovery`].
        recovery: Recovery,
    },
}

impl ToolOutcome {
    /// A successful call that produced `items`, or an [`Self::Empty`] carrying
    /// `when_none` if it produced nothing.
    ///
    /// The reason has to be supplied up front rather than chosen afterwards,
    /// so that a caller cannot discover it has zero items and reach for
    /// whichever reason is nearest.
    pub const fn from_items(items: u32, when_none: EmptyReason) -> Self {
        match NonZeroU32::new(items) {
            Some(items) => Self::Ok { items },
            None => Self::Empty(when_none),
        }
    }

    /// A refusal, with the recovery the result code alone implies.
    pub const fn refused(code: ActionResultCode) -> Self {
        Self::Refused {
            code,
            recovery: recovery_for(code),
        }
    }

    /// A refusal whose recovery a later rule narrowed.
    ///
    /// The narrowing is applied here rather than trusted to the caller:
    /// [`Recovery::stricter_of`] cannot loosen what the code already said, so
    /// there is no argument a caller could pass that makes a policy denial
    /// worth another look.
    pub const fn refused_with(code: ActionResultCode, recovery: Recovery) -> Self {
        Self::Refused {
            code,
            recovery: recovery_for(code).stricter_of(recovery),
        }
    }

    /// A short, compiled-in name for the shape, safe to record in an audit
    /// event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Ok { .. } => "ok",
            Self::Empty(_) => "empty",
            Self::Refused { .. } => "refused",
        }
    }

    /// How many items came back. Zero for both of the other shapes, which is
    /// why it is never the thing a caller reads to decide what happened.
    pub const fn items(self) -> u32 {
        match self {
            Self::Ok { items } => items.get(),
            Self::Empty(_) | Self::Refused { .. } => 0,
        }
    }

    /// Whether this outcome supports the claim that there was nothing to find.
    ///
    /// A refusal never does — the call did not look. An empty result does only
    /// when its reason says so.
    pub const fn is_evidence_of_absence(self) -> bool {
        match self {
            Self::Empty(reason) => reason.is_evidence_of_absence(),
            Self::Ok { .. } | Self::Refused { .. } => false,
        }
    }

    /// What the caller may do next, for a refusal.
    pub const fn recovery(self) -> Option<Recovery> {
        match self {
            Self::Refused { recovery, .. } => Some(recovery),
            Self::Ok { .. } | Self::Empty(_) => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{EmptyReason, ToolOutcome};
    use crate::tool::recovery::Recovery;
    use bip_types::ActionResultCode;

    #[test]
    fn a_successful_outcome_cannot_be_empty() {
        // Not an assertion about a check — an assertion about the type. There
        // is no value of `Ok` with a zero count to construct.
        let outcome = ToolOutcome::from_items(0, EmptyReason::NothingMatched);
        assert_eq!(outcome, ToolOutcome::Empty(EmptyReason::NothingMatched));
        assert_eq!(outcome.items(), 0);
        assert_eq!(
            ToolOutcome::from_items(3, EmptyReason::BoundSpent).items(),
            3
        );
    }

    #[test]
    fn exactly_one_empty_reason_is_evidence_that_nothing_was_there() {
        let evidence: Vec<&str> = EmptyReason::ALL
            .iter()
            .filter(|reason| reason.is_evidence_of_absence())
            .map(|reason| reason.label())
            .collect();
        assert_eq!(evidence, vec!["nothing_matched"]);
    }

    #[test]
    fn an_empty_result_from_a_spent_bound_never_travels_as_absence() {
        for reason in [
            EmptyReason::ScopeExcludedEverything,
            EmptyReason::NotObserved,
            EmptyReason::WithheldByClassification,
            EmptyReason::BoundSpent,
        ] {
            assert!(
                !ToolOutcome::Empty(reason).is_evidence_of_absence(),
                "{}",
                reason.label()
            );
        }
        assert!(ToolOutcome::Empty(EmptyReason::NothingMatched).is_evidence_of_absence());
    }

    #[test]
    fn a_refusal_is_never_evidence_about_the_world() {
        // A refused call did not look. Treating it as "nothing found" is the
        // same mistake as treating a withheld page as a blank one.
        for code in ActionResultCode::ALL {
            let outcome = ToolOutcome::refused(*code);
            assert!(!outcome.is_evidence_of_absence(), "{code:?}");
            assert_eq!(outcome.items(), 0, "{code:?}");
        }
    }

    #[test]
    fn a_refusal_carries_the_recovery_its_code_implies() {
        let outcome = ToolOutcome::refused(ActionResultCode::BudgetExceeded);
        assert_eq!(outcome.recovery(), Some(Recovery::NarrowAndRetry));
        assert_eq!(outcome.label(), "refused");
    }

    #[test]
    fn narrowing_a_refusal_can_tighten_it_and_never_loosen_it() {
        // A policy denial counted again does not become a thing worth
        // observing the page about.
        let tightened = ToolOutcome::refused_with(ActionResultCode::NodeGone, Recovery::Abandon);
        assert_eq!(tightened.recovery(), Some(Recovery::Abandon));
        let unloosened = ToolOutcome::refused_with(
            ActionResultCode::DeniedByPolicy,
            Recovery::ReobserveThenRetry,
        );
        assert_eq!(unloosened.recovery(), Some(Recovery::DoNotRetry));
    }

    #[test]
    fn only_a_refusal_has_a_recovery() {
        assert_eq!(
            ToolOutcome::from_items(2, EmptyReason::NothingMatched).recovery(),
            None
        );
        assert_eq!(ToolOutcome::Empty(EmptyReason::BoundSpent).recovery(), None);
    }

    #[test]
    fn every_shape_and_reason_has_a_distinct_compiled_in_label() {
        let mut seen: Vec<&str> = Vec::new();
        for reason in EmptyReason::ALL {
            assert!(!seen.contains(&reason.label()), "{}", reason.label());
            seen.push(reason.label());
        }
        assert_eq!(
            ToolOutcome::from_items(1, EmptyReason::NothingMatched).label(),
            "ok"
        );
        assert_eq!(
            ToolOutcome::Empty(EmptyReason::NotObserved).label(),
            "empty"
        );
    }
}
