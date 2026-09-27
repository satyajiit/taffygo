// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Precondition evaluation and the stale-node sequence (protocol specification
//! sections 11.4 and 12).
//!
//! [`evaluate_dispatch`] is a pure function. It takes what the task runtime
//! asked for, what the broker knows about its own authority, and what the
//! browser observes right now, and it returns either "proceed" or the exact
//! [`ActionResultCode`] the refusal carries. It reads no clock, holds no state,
//! and performs no effect, so the whole of the specification's step sequence
//! can be exercised as a table.
//!
//! # The sequence
//!
//! Section 12 orders the checks, and the order is load-bearing: a stale epoch
//! must be reported as a stale epoch rather than as a missing node, because the
//! two lead the task runtime to different recoveries. [`StaleNodeStep`] names
//! the step a refusal stopped at so an audit record can say which check failed
//! without carrying anything about the page.
//!
//! Steps one through six are decisions over values and live here. Steps seven
//! through ten — journal the intent, dispatch, observe the postconditions,
//! record the terminal result and consume the capability — are effects, and
//! what each of their outcomes means is decided by
//! [`crate::sequence::StaleNodeSequence`], which consumes the verdict this
//! function returns and carries the same action to exactly one result code.
//!
//! # What a refusal is worth
//!
//! Every code this function returns is a refusal before anything reached the
//! page: `side_effect()` on each of them is `NotPerformed`. That is what makes
//! a re-observation and a fresh proposal safe after one. The task runtime must
//! still not retry the old handle, resolve the target by selector, text,
//! ordinal, or coordinates, broaden origin scope, or reuse an approval whose
//! target or destination changed.
//!
//! # How this module is laid out
//!
//! | Module | Owns |
//! |---|---|
//! | [`observed`] | What the browser reports right now, and what the broker knows about its own books |
//! | [`check`] | What the capability bound and the proposal declared |
//! | [`verdict`] | Proceed, or the exact code a refusal carries |
//! | [`sequence`] | Steps one through six, in the order section 12 fixes |
//!
//! What is left here is the proposal's own declared preconditions, which are
//! evaluated after the sequence and are a total match over
//! [`PreconditionKind`].

mod check;
mod observed;
mod sequence;
mod verdict;

pub use self::check::DispatchCheck;
pub use self::observed::{
    BrokerStanding, BudgetState, CapabilityStanding, FramePresence, LeaseStanding,
    NormalizedDestination, ObservedNode, ObservedState, TabPresence, UserInteraction,
};
pub use self::sequence::evaluate_dispatch;
pub use self::verdict::DispatchVerdict;

use bip_types::action::{Precondition, PreconditionKind};
use bip_types::identity::DocumentLifecycleState;
use bip_types::snapshot::NodeState;
use bip_types::ActionResultCode;

/// Evaluates one declared precondition.
///
/// Returns the refusal code, or `None` when the precondition holds. A
/// precondition whose operand for its kind is absent fails closed: the kind
/// selects the operand, and an operand that does not belong to the kind is
/// never treated as satisfied by default.
pub fn evaluate_precondition(
    precondition: &Precondition,
    check: &DispatchCheck<'_>,
    standing: BrokerStanding,
    observed: &ObservedState,
) -> Option<ActionResultCode> {
    match precondition.kind {
        PreconditionKind::ExactPageEpoch => match precondition.page_epoch.as_ref() {
            Some(epoch) if *epoch == observed.page_epoch => None,
            Some(_) => Some(ActionResultCode::StalePageEpoch),
            None => Some(ActionResultCode::Unsupported),
        },
        PreconditionKind::AcceptableGraphRevision => match precondition.min_graph_revision {
            Some(minimum) if observed.graph_revision.0 >= minimum.0 => None,
            Some(_) => Some(ActionResultCode::StaleGraph),
            None => Some(ActionResultCode::Unsupported),
        },
        PreconditionKind::ExactOrigin => match precondition.origin.as_ref() {
            Some(origin) => match crate::origin::normalize(origin) {
                Ok(expected) if expected.is_same_origin(&observed.origin) => None,
                Ok(_) => Some(ActionResultCode::OriginChanged),
                Err(_) => Some(ActionResultCode::Unsupported),
            },
            None => Some(ActionResultCode::Unsupported),
        },
        PreconditionKind::AllowedRedirectSet => match precondition.allowed_origins.as_ref() {
            Some(origins) => match crate::origin::AllowedRedirects::from_wire(origins) {
                // The landed origin is the observed one; an empty set forbids
                // every redirect, so a document that is no longer on the
                // required origin has nowhere to be permitted from.
                Ok(allowed) => {
                    if observed.origin.is_same_origin(check.required_origin)
                        || allowed.permits(&observed.origin)
                    {
                        None
                    } else {
                        Some(ActionResultCode::OriginChanged)
                    }
                }
                Err(_) => Some(ActionResultCode::Unsupported),
            },
            None => Some(ActionResultCode::Unsupported),
        },
        PreconditionKind::DocumentActive => {
            if observed.lifecycle == DocumentLifecycleState::Active {
                None
            } else {
                Some(ActionResultCode::DocumentInactive)
            }
        }
        PreconditionKind::NodeExists
        | PreconditionKind::NodeRoleUnchanged
        | PreconditionKind::NodeActionAvailable
        | PreconditionKind::NodeStateAsserted
        | PreconditionKind::NodeStateAbsent
        | PreconditionKind::ExpectedDestination
        | PreconditionKind::ExpectedValueDigest
        | PreconditionKind::NotSensitiveField => {
            evaluate_node_precondition(precondition, check, observed)
        }
        PreconditionKind::DestinationClassAllowed
        | PreconditionKind::ContentTrustAtLeast
        | PreconditionKind::PreparedEffectUnchanged
        | PreconditionKind::NoUndeclaredEgress => {
            evaluate_flow_precondition(precondition, standing, observed)
        }
        PreconditionKind::NoUserInteractionSinceLease => {
            if observed.user_interaction == UserInteraction::NoneSinceLease {
                None
            } else {
                Some(ActionResultCode::CancelledByUser)
            }
        }
        PreconditionKind::BudgetRemaining => {
            if observed.budget == BudgetState::Remaining {
                None
            } else {
                Some(ActionResultCode::BudgetExceeded)
            }
        }
    }
}

/// Evaluates the preconditions that are about the node rather than about the
/// document.
///
/// Split out from [`evaluate_precondition`] only so each function stays
/// readable; it, [`evaluate_precondition`] and [`evaluate_flow_precondition`]
/// together are the complete, total match over [`PreconditionKind`].
fn evaluate_node_precondition(
    precondition: &Precondition,
    check: &DispatchCheck<'_>,
    observed: &ObservedState,
) -> Option<ActionResultCode> {
    match precondition.kind {
        PreconditionKind::NodeExists => match observed.node.as_ref() {
            Some(_) => None,
            None => Some(ActionResultCode::NodeGone),
        },
        PreconditionKind::NodeRoleUnchanged => {
            match (precondition.expected_role, observed.node.as_ref()) {
                (Some(role), Some(node)) if node.role == role => None,
                (Some(_), Some(_)) => Some(ActionResultCode::RoleOrActionChanged),
                (Some(_), None) => Some(ActionResultCode::NodeGone),
                (None, _) => Some(ActionResultCode::Unsupported),
            }
        }
        PreconditionKind::NodeActionAvailable => {
            match (precondition.expected_action_type, observed.node.as_ref()) {
                (Some(action), Some(node)) if node.available_actions.contains(&action) => None,
                (Some(_), Some(_)) => Some(ActionResultCode::RoleOrActionChanged),
                (Some(_), None) => Some(ActionResultCode::NodeGone),
                (None, _) => Some(ActionResultCode::Unsupported),
            }
        }
        PreconditionKind::NodeStateAsserted => {
            match (precondition.node_state, observed.node.as_ref()) {
                (Some(state), Some(node)) if node.asserts(state) => None,
                (Some(state), Some(_)) => Some(state_refusal(state)),
                (Some(_), None) => Some(ActionResultCode::NodeGone),
                (None, _) => Some(ActionResultCode::Unsupported),
            }
        }
        PreconditionKind::NodeStateAbsent => {
            match (precondition.node_state, observed.node.as_ref()) {
                (Some(state), Some(node)) if !node.asserts(state) => None,
                (Some(state), Some(_)) => Some(state_refusal(state)),
                (Some(_), None) => Some(ActionResultCode::NodeGone),
                (None, _) => Some(ActionResultCode::Unsupported),
            }
        }
        PreconditionKind::ExpectedDestination => match check.expected_destination {
            Some(expected) => match observed
                .node
                .as_ref()
                .and_then(|node| node.destination.as_ref())
            {
                Some(current) if current == expected => None,
                _ => Some(ActionResultCode::DestinationChanged),
            },
            None => Some(ActionResultCode::Unsupported),
        },
        PreconditionKind::ExpectedValueDigest => {
            match (
                precondition.expected_value_digest.as_ref(),
                observed
                    .node
                    .as_ref()
                    .and_then(|node| node.value_digest.as_ref()),
            ) {
                (Some(expected), Some(current))
                    if expected.algorithm == current.algorithm
                        && expected.value == current.value =>
                {
                    None
                }
                (Some(_), Some(_)) => Some(ActionResultCode::PostconditionFailed),
                (Some(_), None) => Some(ActionResultCode::NodeGone),
                (None, _) => Some(ActionResultCode::Unsupported),
            }
        }
        PreconditionKind::NotSensitiveField => match observed.node.as_ref() {
            Some(node) if node.sensitivity.is_empty() => None,
            Some(_) => Some(ActionResultCode::SensitiveField),
            None => Some(ActionResultCode::NodeGone),
        },
        PreconditionKind::ExactPageEpoch
        | PreconditionKind::AcceptableGraphRevision
        | PreconditionKind::ExactOrigin
        | PreconditionKind::AllowedRedirectSet
        | PreconditionKind::DocumentActive
        | PreconditionKind::NoUserInteractionSinceLease
        | PreconditionKind::BudgetRemaining
        | PreconditionKind::DestinationClassAllowed
        | PreconditionKind::ContentTrustAtLeast
        | PreconditionKind::PreparedEffectUnchanged
        | PreconditionKind::NoUndeclaredEgress => Some(ActionResultCode::Unsupported),
    }
}

/// Evaluates the preconditions that are about where content came from and
/// where an effect would go, rather than about the node's identity.
///
/// Its own function because these two are the only kinds whose operand is not
/// the observation at all: one reads a compiled-in table, the other reads a
/// label the renderer attached. Both refuse and neither can permit anything — a
/// precondition that holds still leaves every other check standing.
fn evaluate_flow_precondition(
    precondition: &Precondition,
    standing: BrokerStanding,
    observed: &ObservedState,
) -> Option<ActionResultCode> {
    match precondition.kind {
        // The class table is compiled in, so this kind takes no operand. A
        // precondition carrying its own table would be a table a message could
        // rewrite, which is the whole thing the table exists to prevent.
        PreconditionKind::DestinationClassAllowed => match observed
            .node
            .as_ref()
            .and_then(|node| node.destination.as_ref())
        {
            Some(destination) => crate::site::classify_site(&destination.origin)
                .map(|_| ActionResultCode::DestinationClassRestricted),
            // A class check on a node with no destination has nothing to check,
            // and refuses rather than passing vacuously.
            None => Some(ActionResultCode::Unsupported),
        },
        PreconditionKind::ContentTrustAtLeast => {
            match (precondition.min_content_trust, observed.node.as_ref()) {
                (Some(floor), Some(node)) => {
                    // The floor names an author the target must not carry. An
                    // unnameable author fails too: absence of a label is not
                    // evidence of a trustworthy one.
                    if node.content_trust.contains(floor) || node.content_trust.has_unknown_author()
                    {
                        Some(ActionResultCode::UntrustedContentOrigin)
                    } else {
                        None
                    }
                }
                (Some(_), None) => Some(ActionResultCode::NodeGone),
                (None, _) => Some(ActionResultCode::Unsupported),
            }
        }
        // The commit half of a prepared effect. The comparison against the
        // ledger is not made here — this evaluator holds no ledger, and a
        // message cannot present one — so it is made by
        // [`crate::prepared::plan_commit`] before the sequence runs and arrives
        // as [`BrokerStanding::prepared`]. What is left here is the ordering
        // check, which is a property of the binding alone: a gesture that does
        // not postdate the prepare cannot have been a response to it, and that
        // is what makes a programmatic commit impossible rather than merely
        // unlikely.
        //
        // The ordering check runs first and independently of the ledger's
        // answer, so a binding whose gesture is spliced on backwards is refused
        // as such even when the record it names is otherwise perfect.
        //
        // [`PreparedEffectStanding::NotChecked`] refuses. A caller that
        // consulted no ledger has compared nothing, and reporting the effect
        // unchanged would claim a comparison nothing performed.
        PreconditionKind::PreparedEffectUnchanged => match precondition.prepared_effect.as_ref() {
            Some(binding) => {
                if binding.gesture_at_monotonic_ms <= binding.prepared_at_monotonic_ms {
                    Some(ActionResultCode::CommitWithoutPrepare)
                } else {
                    standing.prepared.refusal()
                }
            }
            None => Some(ActionResultCode::Unsupported),
        },
        // Corroborated by the browser's own network stack, not by the
        // dispatch-time observation, which sees no requests at all. The kind is
        // recognized; what is missing is the witness, so the refusal names the
        // check rather than claiming the check is unknown.
        PreconditionKind::NoUndeclaredEgress => Some(ActionResultCode::EgressNotAuthorized),
        PreconditionKind::ExactPageEpoch
        | PreconditionKind::AcceptableGraphRevision
        | PreconditionKind::ExactOrigin
        | PreconditionKind::AllowedRedirectSet
        | PreconditionKind::DocumentActive
        | PreconditionKind::NodeExists
        | PreconditionKind::NodeRoleUnchanged
        | PreconditionKind::NodeActionAvailable
        | PreconditionKind::NodeStateAsserted
        | PreconditionKind::NodeStateAbsent
        | PreconditionKind::ExpectedDestination
        | PreconditionKind::ExpectedValueDigest
        | PreconditionKind::NotSensitiveField
        | PreconditionKind::NoUserInteractionSinceLease
        | PreconditionKind::BudgetRemaining => Some(ActionResultCode::Unsupported),
    }
}

/// The result code that reports a state assertion the node did not satisfy.
const fn state_refusal(state: NodeState) -> ActionResultCode {
    match state {
        NodeState::Visible | NodeState::NotVisible | NodeState::Offscreen => {
            ActionResultCode::NotVisible
        }
        NodeState::Obscured => ActionResultCode::Occluded,
        NodeState::Enabled | NodeState::Disabled | NodeState::Busy => ActionResultCode::NotEnabled,
        NodeState::Editable | NodeState::ReadOnly => ActionResultCode::NotEditable,
        NodeState::Required
        | NodeState::Invalid
        | NodeState::Checked
        | NodeState::Unchecked
        | NodeState::Mixed
        | NodeState::Selected
        | NodeState::Expanded
        | NodeState::Collapsed
        | NodeState::Focused => ActionResultCode::PostconditionFailed,
    }
}
