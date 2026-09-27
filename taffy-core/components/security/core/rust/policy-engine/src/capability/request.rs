// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a caller asks for when it wants authority (domain model section 12.4).
//!
//! Every field of [`CapabilityScope`] narrows. A scope with a node authorizes
//! one node; one without it authorizes a document-level effect such as a scroll
//! or a task tab, and never a node-targeted action. A scope without a
//! destination authorizes no navigation target at all, so a node that acquires
//! one between observation and dispatch fails the action closed.
//!
//! [`CapabilityRequest::effective_risk`] is where a caller's declared context
//! meets the class's own baseline for the phase being asked about, and the
//! answer is a join: a caller cannot make a proposal cheaper by declaring it
//! so.

use bip_types::action::Principal;
use bip_types::identity::{
    ApprovalReceiptReference, ContentDigest, FrameId, GraphRevision, MonotonicMillis, PageEpoch,
    ProfileId, SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;

use crate::action_class::{ActionClass, PolicyMilestone};
use crate::approval::{ApprovalBinding, ApprovalId};
use crate::capability::PolicyVersion;
use crate::origin::{AllowedRedirects, NormalizedOrigin};
use crate::phase::ActionPhase;
use crate::risk::RiskClass;

/// The exact place in the browser a capability authorizes.
///
/// Every field narrows. A capability with `node_id` set authorizes one node;
/// one without it authorizes a document-level effect such as a scroll or a task
/// tab, and never a node-targeted action.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CapabilityScope {
    /// The profile the effect happens in.
    pub profile_id: ProfileId,
    /// The tab the effect happens in.
    pub tab_id: TabId,
    /// The frame the effect happens in.
    pub frame_id: FrameId,
    /// The document instance. A new epoch is a new document, and this
    /// capability does not reach it.
    pub page_epoch: PageEpoch,
    /// The normalized origin the effect is bound to.
    pub origin: NormalizedOrigin,
    /// The node, for a node-targeted action.
    pub node_id: Option<SemanticNodeId>,
    /// Where a navigation is authorized to land, for a destination-bearing
    /// action (domain model section 12.4). `None` authorizes no navigation
    /// target, so a node that acquires one between observation and dispatch
    /// fails the action closed.
    pub destination_scope: Option<NormalizedOrigin>,
    /// Exact destination address, including path/query/fragment, when the
    /// operation carries one. Retained byte-for-byte for spending.
    pub destination_address: Option<String>,
    /// The graph revision the observation was taken at. A dispatch needs a
    /// revision at least this new.
    pub required_graph_revision: GraphRevision,
    /// Where a navigation may land. Empty forbids redirects.
    pub allowed_redirects: AllowedRedirects,
}

/// What a caller asks for when it wants authority.
#[derive(Clone, Debug, PartialEq)]
pub struct CapabilityRequest {
    /// The task the authority belongs to.
    pub task_id: TaskId,
    /// The assistant, plus the exact skill version when one is active.
    pub principal: Principal,
    /// What the effect does.
    pub action_class: ActionClass,
    /// Which half of a two-step authorization this request is (decision 0022).
    ///
    /// A property of the request, never of the class. A single-step action is a
    /// commit: nothing was staged, so there is no cheaper reading to take.
    pub phase: ActionPhase,
    /// A digest over the exact proposal being authorized. The authorization is
    /// for this proposal and no other.
    pub action_digest: ContentDigest,
    /// Where the effect may happen.
    pub scope: CapabilityScope,
    /// The data classes the action discloses, joined from every signal.
    pub data_classes: SensitivitySet,
    /// The risk the context adds, over and above the class's own baseline.
    pub context_risk: RiskClass,
    /// The approval to spend, where the proposal has one.
    pub approval: Option<ApprovalId>,
    /// When the authority stops standing, on the monotonic clock.
    pub expires_at: MonotonicMillis,
}

impl CapabilityRequest {
    /// How consequential this proposal is once its context is taken into
    /// account.
    ///
    /// The join of the class's baseline *in this phase* and whatever the
    /// context added. Risk only rises, so a caller cannot make a proposal
    /// cheaper by declaring it so — the phase selects a row of a compiled-in
    /// table and nothing here lowers what the table answers.
    pub fn effective_risk(&self) -> RiskClass {
        self.action_class
            .baseline_risk_in(self.phase)
            .join(self.context_risk)
    }

    /// The question an approval for this proposal has to have answered.
    ///
    /// Derived from the request being authorized, never from the approval, so
    /// an answer given to a different question cannot be presented here.
    pub fn approval_binding(&self, effective_risk: RiskClass) -> ApprovalBinding {
        ApprovalBinding {
            action_class: self.action_class,
            phase: self.phase,
            action_digest: self.action_digest.clone(),
            tab_id: self.scope.tab_id.clone(),
            frame_id: self.scope.frame_id.clone(),
            page_epoch: self.scope.page_epoch.clone(),
            origin: self.scope.origin.clone(),
            node_id: self.scope.node_id.clone(),
            destination: self.scope.destination_scope.clone(),
            data_classes: self.data_classes,
            risk: effective_risk,
        }
    }
}

/// What the broker had already decided before the ledger records a capability.
///
/// Bundled rather than passed one at a time so the ledger's own signature
/// cannot drift into taking a bag of loose flags, and so a caller cannot supply
/// a milestone or a policy version that came from anywhere but the broker.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct IssueContext {
    /// The ratified surface the decision was made against.
    pub milestone: PolicyMilestone,
    /// The policy bundle version in force.
    pub policy_version: PolicyVersion,
    /// The approval that was spent, where one was required.
    pub approval: Option<ApprovalReceiptReference>,
}
