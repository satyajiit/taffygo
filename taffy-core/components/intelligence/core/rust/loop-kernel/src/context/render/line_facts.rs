// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Two facts about one node's line, read before a number is issued for it:
//! whether it says anything at all, and whether `user.request_values` may name
//! it.

use std::collections::BTreeSet;

use bip_types::action::ActionType;
use bip_types::snapshot::{NodeState, SemanticRole};

use task_engine::handle::ValueTarget;

use crate::context::arena::ArenaNode;
use crate::context::vocabulary::sensitivity_words;

/// What a person's values could go into on this line, which decides what
/// `user.request_values` and `browser.form.fill` may name (decisions 0192 and
/// 0195).
///
/// A field that takes text is a field. A form shown fields belong to is a
/// container, and so is a region or an unclassified node, because it may be a
/// form the arena cannot recognise: the myAadhaar download form has no address
/// to go to, so its fields name no container here, and it was naming that
/// region that drew the sheet. Every other role is one the browser gives up on
/// without drawing anything, and is refused before it gets there.
pub(super) fn value_target(node: &ArenaNode, containers: &BTreeSet<&str>) -> ValueTarget {
    if node.actions.contains(&ActionType::SetText) {
        ValueTarget::Field
    } else if containers.contains(node.node_id.as_str())
        || matches!(
            node.role,
            SemanticRole::Region | SemanticRole::UnknownContent | SemanticRole::UnknownInteractive
        )
    {
        ValueTarget::Container
    } else {
        ValueTarget::None
    }
}

/// Whether a node's line would tell the model nothing.
///
/// No label and no text, nothing withheld, nowhere it leads, no private
/// class, no form it belongs to, no state beyond being hidden or off screen,
/// and nothing it can be asked to do but scroll: `content (authored by
/// first-party document) — can scroll`. On the myAadhaar home page 263 of the
/// 381 lines a snapshot printed were this, the budget ran out on them, and the
/// page's own buttons were among the 304 nodes it never reached — so the model
/// named numbers it had never been shown (decision 0189). Such a node is not
/// omitted for room: asking again with more room would print the same
/// nothing, so the footer counts it apart.
pub(super) fn says_nothing(node: &ArenaNode) -> bool {
    node.name.is_none()
        && !node.name_withheld
        && node.text.is_empty()
        && !node.text_withheld
        && node.declared_text_runs == 0
        && !node.destination.present()
        && node.container.is_none()
        && sensitivity_words(node.sensitivity).is_none()
        && node
            .actions
            .iter()
            .all(|action| *action == ActionType::ScrollIntoView)
        && node.states.iter().all(|state| {
            matches!(
                state,
                NodeState::Visible
                    | NodeState::Enabled
                    | NodeState::NotVisible
                    | NodeState::Offscreen
            )
        })
}
