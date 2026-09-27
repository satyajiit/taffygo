// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The action result taxonomy and its classifiers (specification section 11.7).
//!
//! The taxonomy itself is generated from `taffy-core/contracts/bip/schema/action.schema.json`
//! and re-exported here in the order the specification lists it, which is
//! normative. What this module adds is the reading of each code that every
//! consumer would otherwise re-derive, differently, in its own `match`.
//!
//! Result codes are security-significant. Three rules hold:
//!
//! - an unknown code is not a code. [`ActionResultCode::decode`] yields
//!   [`Decoded::Unsupported`], and there is no path from there back to a known
//!   member;
//! - only [`ActionResultCode::Verified`] means the action happened as
//!   authorized. Renderer acknowledgement alone is a dispatch, not a
//!   verification (section 11.6);
//! - user-facing text is generated from trusted local templates. Nothing here
//!   carries a renderer-supplied string, and nothing here should be shown to a
//!   user directly.

use crate::version::{ClosedEnum, Decoded};

pub use crate::generated::action::ActionResultCode;

/// What a result code says about the page.
///
/// The distinction that matters is not success versus failure but whether a
/// side effect may have reached the page, because that decides whether a retry
/// is safe. Section 13 forbids automatic consequential replay, so
/// [`Self::Possible`] and [`Self::Performed`] are both non-replayable.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SideEffectCertainty {
    /// The action was refused before anything reached the page. The task
    /// runtime may propose again, subject to policy, without risking a repeat.
    NotPerformed,
    /// A side effect may have reached the page and cannot be ruled out. The
    /// runtime must not replay a consequential action on this outcome.
    Possible,
    /// The action happened and its declared postconditions were observed.
    Performed,
}

impl ActionResultCode {
    /// Decodes the stable numeric value used by browser journals.
    ///
    /// The generated `ALL` table is in schema declaration order, which is the
    /// same append-only order the browser persists. Returning `None` keeps an
    /// added or corrupt value closed at the storage boundary.
    pub fn from_ordinal(value: u32) -> Option<Self> {
        usize::try_from(value)
            .ok()
            .and_then(|index| Self::ALL.get(index).copied())
    }

    /// Decodes a wire value, yielding an explicit unsupported outcome for
    /// anything outside the taxonomy.
    ///
    /// An unrecognised code is never coerced to a neighbouring one and never
    /// treated as [`Self::Verified`]. Section 11.7: unknown codes fail closed.
    pub fn decode(value: &str) -> Decoded<Self> {
        <Self as ClosedEnum>::decode_wire(value)
    }

    /// Whether the runtime must treat the action as not authorized-and-complete.
    ///
    /// True for every code except [`Self::Verified`]. That is the point of the
    /// classifier rather than a weakness of it: the safe default is refusal, so
    /// the one code that grants a success claim is named explicitly and
    /// everything else — including codes added by a future minor version, which
    /// do not decode at all — lands on the closed side.
    ///
    /// Failing closed means the runtime must not report success, must not
    /// consider a declared postcondition satisfied, and must not consume the
    /// outcome as evidence that the user's goal was met.
    pub fn fails_closed(self) -> bool {
        !matches!(self, Self::Verified)
    }

    /// Whether the browser cannot prove if this attempt changed the page.
    ///
    /// A durable reconciliation may replace an `OutcomeUnknown` action only
    /// with a result for which this is false. Otherwise it has merely found a
    /// more specific name for the same ambiguity and must still ask a person.
    pub fn has_uncertain_side_effect(self) -> bool {
        matches!(self.side_effect(), SideEffectCertainty::Possible)
    }

    /// Whether this code ends the action's lifecycle.
    ///
    /// A terminal code is the runtime's final answer for one action identifier:
    /// no further outcome will arrive and the capability is spent. Two codes are
    /// not terminal, and both name something the runtime is still waiting for:
    ///
    /// - [`Self::ApprovalRequired`] — the proposal is parked on a user
    ///   decision, which arrives as a separate authorization or an
    ///   [`Self::ApprovalDenied`];
    /// - [`Self::NavigationStarted`] — a browser-owned navigation began and its
    ///   postconditions are still being verified against the new document.
    ///
    /// Section 6.3 delivers exactly one terminal response per dispatch. A
    /// non-terminal code is therefore a statement about the action, not about
    /// the request that carried it.
    pub fn is_terminal(self) -> bool {
        !matches!(self, Self::ApprovalRequired | Self::NavigationStarted)
    }

    /// Whether the outcome is still being determined elsewhere.
    ///
    /// The complement of [`Self::is_terminal`], named so a caller does not have
    /// to negate a predicate to express waiting.
    pub fn awaits_further_outcome(self) -> bool {
        !self.is_terminal()
    }

    /// What the code says about the page.
    pub fn side_effect(self) -> SideEffectCertainty {
        match self {
            Self::Verified => SideEffectCertainty::Performed,
            // Dispatched, or possibly dispatched, with the effect unconfirmed
            // or contradicted. A consequential action is never replayed on any
            // of these (section 13).
            Self::DispatchFailed
            | Self::NavigationStarted
            | Self::PostconditionTimeout
            | Self::PostconditionFailed
            | Self::CancelledByNavigation
            | Self::RendererCrashed
            | Self::OutcomeUnknown
            | Self::InternalError => SideEffectCertainty::Possible,
            // Refused by policy, by the handle checks of section 12, or by the
            // node's own state, all before anything reached the page. A
            // cancellation that races a dispatch is reported as
            // OUTCOME_UNKNOWN, not as a cancellation.
            Self::DeniedByPolicy
            | Self::ApprovalRequired
            | Self::ApprovalDenied
            | Self::ActorLeaseMissing
            | Self::CapabilityExpired
            | Self::TabGone
            | Self::FrameGone
            | Self::DocumentInactive
            | Self::StalePageEpoch
            | Self::StaleGraph
            | Self::NodeGone
            | Self::OriginChanged
            | Self::RoleOrActionChanged
            | Self::NotVisible
            | Self::Occluded
            | Self::NotEnabled
            | Self::NotEditable
            | Self::SensitiveField
            | Self::DestinationChanged
            | Self::EgressNotAuthorized
            | Self::DestinationClassRestricted
            | Self::UntrustedContentOrigin
            | Self::PreparedEffectChanged
            | Self::CommitWithoutPrepare
            | Self::Unsupported
            | Self::BudgetExceeded
            // The browser knew the document had moved and refused before
            // anything was dispatched. That is the whole content of the code:
            // it is a preflight refusal, so nothing reached the page.
            | Self::GraphMovedDuringPreflight
            // Resolution happens after journalling and before the renderer
            // send. An unknown/spent/expired/misbound reference therefore
            // reached no page even though the attempt has a durable intent.
            | Self::ValueReferenceUnknown
            | Self::CancelledByUser => SideEffectCertainty::NotPerformed,
        }
    }

    /// Whether the code reports a handle that no longer refers to the observed
    /// node.
    ///
    /// Exactly the three codes section 12 names. On any of them the core
    /// service may request a fresh observation and propose a new action. It must not
    /// retry the old handle, re-resolve the target by selector, text, ordinal,
    /// or coordinates, broaden origin scope, reuse an approval whose target or
    /// destination changed, or report success because the old target looked
    /// similar.
    pub fn is_stale_handle(self) -> bool {
        matches!(
            self,
            Self::StalePageEpoch | Self::StaleGraph | Self::NodeGone
        )
    }

    /// Whether the runtime is waiting on the user rather than on the page.
    pub fn requires_user_decision(self) -> bool {
        matches!(self, Self::ApprovalRequired | Self::ValueReferenceUnknown)
    }

    /// Whether the user, not the browser, ended the action.
    ///
    /// Take over revokes undispatched authority, so these outcomes are never
    /// retried on the runtime's own initiative.
    pub fn is_user_cancellation(self) -> bool {
        matches!(self, Self::CancelledByUser | Self::ApprovalDenied)
    }
}

#[cfg(test)]
mod tests {
    use super::{ActionResultCode, SideEffectCertainty};

    #[test]
    fn only_a_verified_action_may_be_reported_as_success() {
        assert!(!ActionResultCode::Verified.fails_closed());
        assert_eq!(
            ActionResultCode::Verified.side_effect(),
            SideEffectCertainty::Performed
        );

        for code in ActionResultCode::ALL {
            if *code == ActionResultCode::Verified {
                continue;
            }
            assert!(code.fails_closed(), "{} must fail closed", code.wire());
        }
    }

    #[test]
    fn every_persisted_ordinal_decodes_and_the_boundary_stays_closed() {
        for (ordinal, expected) in ActionResultCode::ALL.iter().copied().enumerate() {
            let ordinal = u32::try_from(ordinal).expect("the result table fits u32");
            assert_eq!(ActionResultCode::from_ordinal(ordinal), Some(expected));
        }
        let past_end =
            u32::try_from(ActionResultCode::ALL.len()).expect("the result table length fits u32");
        assert_eq!(ActionResultCode::from_ordinal(past_end), None);
        assert_eq!(ActionResultCode::from_ordinal(u32::MAX), None);
    }

    #[test]
    fn a_refusal_before_dispatch_is_safe_to_propose_again() {
        for code in [
            ActionResultCode::DeniedByPolicy,
            ActionResultCode::CapabilityExpired,
            ActionResultCode::StalePageEpoch,
            ActionResultCode::NodeGone,
            ActionResultCode::SensitiveField,
        ] {
            assert_eq!(code.side_effect(), SideEffectCertainty::NotPerformed);
        }
    }

    #[test]
    fn an_unconfirmed_dispatch_is_never_replayed() {
        for code in [
            ActionResultCode::DispatchFailed,
            ActionResultCode::NavigationStarted,
            ActionResultCode::PostconditionTimeout,
            ActionResultCode::RendererCrashed,
            ActionResultCode::OutcomeUnknown,
        ] {
            assert_eq!(code.side_effect(), SideEffectCertainty::Possible);
            assert!(code.has_uncertain_side_effect());
            assert!(code.fails_closed());
        }
    }

    #[test]
    fn the_two_waiting_outcomes_are_the_only_non_terminal_ones() {
        let waiting: Vec<&str> = ActionResultCode::ALL
            .iter()
            .filter(|code| code.awaits_further_outcome())
            .map(|code| code.wire())
            .collect();
        assert_eq!(waiting, vec!["APPROVAL_REQUIRED", "NAVIGATION_STARTED"]);
    }

    #[test]
    fn the_stale_handle_outcomes_are_the_three_the_specification_names() {
        let stale: Vec<&str> = ActionResultCode::ALL
            .iter()
            .filter(|code| code.is_stale_handle())
            .map(|code| code.wire())
            .collect();
        assert_eq!(stale, vec!["STALE_PAGE_EPOCH", "STALE_GRAPH", "NODE_GONE"]);
    }

    #[test]
    fn a_user_decision_is_distinguished_from_a_page_failure() {
        assert!(ActionResultCode::ApprovalRequired.requires_user_decision());
        assert!(ActionResultCode::ApprovalDenied.is_user_cancellation());
        assert!(ActionResultCode::CancelledByUser.is_user_cancellation());
        assert!(!ActionResultCode::CancelledByNavigation.is_user_cancellation());
    }
}
