// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The section 12 stale-node sequence: steps one through six.
//!
//! The order is load-bearing. A stale epoch must be reported as a stale epoch
//! rather than as a missing node, because the two lead the task runtime to
//! different recoveries — so the first failure wins and the rest of the
//! sequence is not run. A later check on a document that already turned out to
//! be the wrong one would be answering a question about the wrong page.
//!
//! Steps seven through ten are effects and are not here: they belong to
//! [`crate::sequence::StaleNodeSequence`], which consumes the verdict this
//! module returns.

use bip_types::identity::DocumentLifecycleState;
use bip_types::snapshot::NodeState;
use bip_types::ActionResultCode;

use super::check::DispatchCheck;
use super::evaluate_precondition;
use super::observed::{
    BrokerStanding, BudgetState, CapabilityStanding, FramePresence, LeaseStanding,
    NormalizedDestination, ObservedNode, ObservedState, TabPresence,
};
use super::verdict::DispatchVerdict;
use crate::action_class::ActionClass;
use crate::origin::NormalizedOrigin;
use crate::step::StaleNodeStep;
/// Runs the section 12 sequence and the proposal's own preconditions.
///
/// The first failure wins and the rest of the sequence is not run, because a
/// later check on a document that already turned out to be the wrong one would
/// be answering a question about the wrong page.
pub fn evaluate_dispatch(
    check: &DispatchCheck<'_>,
    standing: BrokerStanding,
    observed: &ObservedState,
) -> DispatchVerdict {
    // Step 1 — the tab and the frame.
    if observed.tab == TabPresence::Gone {
        return DispatchVerdict::refuse(
            StaleNodeStep::ResolveTabAndFrame,
            ActionResultCode::TabGone,
        );
    }
    if observed.frame == FramePresence::Gone {
        return DispatchVerdict::refuse(
            StaleNodeStep::ResolveTabAndFrame,
            ActionResultCode::FrameGone,
        );
    }

    // Step 2 — an active document, then the exact epoch. Lifecycle first: a
    // prerendering or frozen document is not a document this action may reach,
    // whatever epoch it carries.
    if observed.lifecycle != DocumentLifecycleState::Active {
        return DispatchVerdict::refuse(
            StaleNodeStep::ConfirmDocumentAndEpoch,
            ActionResultCode::DocumentInactive,
        );
    }
    if observed.page_epoch != *check.required_page_epoch {
        return DispatchVerdict::refuse(
            StaleNodeStep::ConfirmDocumentAndEpoch,
            ActionResultCode::StalePageEpoch,
        );
    }

    // Step 3 — the origin. Equality of normalized origins, never a same-site
    // relaxation and never a broadening of an opaque origin.
    if !observed.origin.is_same_origin(check.required_origin) {
        return DispatchVerdict::refuse(
            StaleNodeStep::ConfirmOrigin,
            ActionResultCode::OriginChanged,
        );
    }

    // Step 4 — lease and capability.
    if standing.lease == LeaseStanding::Missing {
        return DispatchVerdict::refuse(
            StaleNodeStep::ConfirmLeaseAndCapability,
            ActionResultCode::ActorLeaseMissing,
        );
    }
    if standing.capability == CapabilityStanding::NotValid {
        return DispatchVerdict::refuse(
            StaleNodeStep::ConfirmLeaseAndCapability,
            ActionResultCode::CapabilityExpired,
        );
    }

    // Step 5 — resolve the node at a revision at least as new as the one the
    // proposal was built from.
    let node = match (check.node_id, observed.node.as_ref()) {
        (None, _) => None,
        (Some(_), None) => {
            return DispatchVerdict::refuse(StaleNodeStep::ResolveNode, ActionResultCode::NodeGone)
        }
        (Some(requested), Some(node)) => {
            if node.node_id != *requested {
                return DispatchVerdict::refuse(
                    StaleNodeStep::ResolveNode,
                    ActionResultCode::NodeGone,
                );
            }
            if observed.graph_revision.0 < check.required_graph_revision.0 {
                return DispatchVerdict::refuse(
                    StaleNodeStep::ResolveNode,
                    ActionResultCode::StaleGraph,
                );
            }
            Some(node)
        }
    };

    // Step 6 — re-evaluate everything that can change under a live document.
    if let Some(node) = node {
        if let Some(code) = reevaluate_node(check, node) {
            return DispatchVerdict::refuse(StaleNodeStep::ReevaluateNode, code);
        }
    }
    if observed.budget == BudgetState::Exhausted {
        return DispatchVerdict::refuse(
            StaleNodeStep::ReevaluateNode,
            ActionResultCode::BudgetExceeded,
        );
    }
    for precondition in check.preconditions {
        if let Some(code) = evaluate_precondition(precondition, check, standing, observed) {
            return DispatchVerdict::refuse(StaleNodeStep::ReevaluateNode, code);
        }
    }

    DispatchVerdict::Proceed
}

/// Re-evaluates role, availability, sensitivity, interaction state, and
/// destination.
fn reevaluate_node(check: &DispatchCheck<'_>, node: &ObservedNode) -> Option<ActionResultCode> {
    if node.role != check.expected_role {
        return Some(ActionResultCode::RoleOrActionChanged);
    }
    if !node.available_actions.contains(&check.action_type) {
        return Some(ActionResultCode::RoleOrActionChanged);
    }
    // A never-extract classification refuses every action on the node, not just
    // a read of its value: activating a credential control is how a page tries
    // to get one submitted.
    if node.sensitivity.is_never_extract() {
        return Some(ActionResultCode::SensitiveField);
    }

    if requires_interaction(check.action_class) {
        // Absence of VISIBLE is not an assertion of NOT_VISIBLE, and an action
        // policy needs the positive assertion, so absence refuses.
        if !node.asserts(NodeState::Visible) {
            return Some(ActionResultCode::NotVisible);
        }
        if node.asserts(NodeState::Obscured) {
            return Some(ActionResultCode::Occluded);
        }
        if node.asserts(NodeState::Disabled) || !node.asserts(NodeState::Enabled) {
            return Some(ActionResultCode::NotEnabled);
        }
    }

    if let Some(expected) = check.expected_destination {
        match node.destination.as_ref() {
            Some(current) if current == expected => {}
            _ => return Some(ActionResultCode::DestinationChanged),
        }
        if expected.origin != *check.required_origin
            && !check.allowed_redirects.permits(&expected.origin)
        {
            return Some(ActionResultCode::DeniedByPolicy);
        }
    }

    // The capability's own destination scope, checked separately from the
    // proposal's expectation. The proposal says where it thinks the node goes;
    // this says where the authorization allows it to go, and a node that
    // acquired a destination the capability never named is a changed
    // destination whatever the proposal expected.
    if !destination_scope_holds(check.required_destination, node.destination.as_ref()) {
        return Some(ActionResultCode::DestinationChanged);
    }

    None
}

/// Whether the node's current destination is the one the capability authorized.
fn destination_scope_holds(
    authorized: Option<&NormalizedOrigin>,
    observed: Option<&NormalizedDestination>,
) -> bool {
    match (authorized, observed) {
        (None, None) => true,
        (Some(authorized), Some(observed)) => observed.origin.is_same_origin(authorized),
        _ => false,
    }
}

/// Whether the class puts a synthetic interaction on the node.
const fn requires_interaction(class: ActionClass) -> bool {
    !matches!(
        class,
        ActionClass::ObservePage | ActionClass::ScrollIntoView | ActionClass::CreateTaskTab
    )
}
