// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the task runtime is asking to dispatch.
//!
//! Every field is something the capability bound, or something the proposal
//! declared. Nothing here is observed — that is [`super::observed`] — and
//! nothing here is decided. Keeping the request apart from the observation is
//! what makes "does the world still match what was authorized" a comparison of
//! two values rather than a walk through one struct.

use bip_types::action::{ActionType, Precondition};
use bip_types::identity::{FrameId, GraphRevision, PageEpoch, SemanticNodeId, TabId};
use bip_types::snapshot::SemanticRole;

use super::observed::NormalizedDestination;
use crate::action_class::ActionClass;
use crate::origin::{AllowedRedirects, NormalizedOrigin};
/// What the task runtime is asking to dispatch.
#[derive(Clone, Debug, PartialEq)]
pub struct DispatchCheck<'a> {
    /// The tab the capability is bound to.
    pub tab_id: &'a TabId,
    /// The frame the capability is bound to.
    pub frame_id: &'a FrameId,
    /// The document instance the capability is bound to.
    pub required_page_epoch: &'a PageEpoch,
    /// The graph revision the observation was taken at.
    pub required_graph_revision: GraphRevision,
    /// The origin the capability is bound to.
    pub required_origin: &'a NormalizedOrigin,
    /// The navigation target the capability is bound to, when it authorized
    /// one. `None` authorizes no navigation target at all.
    pub required_destination: Option<&'a NormalizedOrigin>,
    /// Where a navigation may land. Empty forbids redirects.
    pub allowed_redirects: &'a AllowedRedirects,
    /// The node the action targets, for a node-targeted class.
    pub node_id: Option<&'a SemanticNodeId>,
    /// The role the node had when it was observed.
    pub expected_role: SemanticRole,
    /// The protocol action type being dispatched.
    pub action_type: ActionType,
    /// The class of effect it carries.
    pub action_class: ActionClass,
    /// The destination the proposal expects, for a destination-bearing node.
    pub expected_destination: Option<&'a NormalizedDestination>,
    /// The proposal's own preconditions, checked after the sequence.
    pub preconditions: &'a [Precondition],
}
