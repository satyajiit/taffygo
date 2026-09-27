// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the browser observes right now, as values.
//!
//! Closed types and nothing else: the presence of a tab and a frame, whether
//! the user has touched the tab, whether budget remains, and what the targeted
//! node currently looks like — plus [`BrokerStanding`], the readings of the
//! broker's own books that the same evaluation needs.
//!
//! Its own module because it is the *input* half of precondition evaluation.
//! Nothing here decides anything, which is what lets the whole of the section
//! 12 sequence be exercised as a table. The two halves are kept apart on
//! purpose: [`ObservedState`] is what the host reported about the world, and
//! [`BrokerStanding`] is what the broker already knew about itself.

use bip_types::action::ActionType;
use bip_types::identity::{
    ContentDigest, DocumentLifecycleState, GraphRevision, PageEpoch, SemanticNodeId,
};
use bip_types::sensitivity::SensitivitySet;
use bip_types::snapshot::{NodeState, SemanticRole};
use bip_types::trust::TrustSet;

use crate::origin::NormalizedOrigin;
use crate::prepared::PreparedEffectStanding;
/// Whether the tab still exists.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TabPresence {
    /// The browser still has this tab.
    Present,
    /// The tab was closed. Its identifier is never reassigned.
    Gone,
}

/// Whether the frame still exists.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FramePresence {
    /// The frame is still attached.
    Present,
    /// The frame was detached or replaced.
    Gone,
}

/// Whether the user has touched the tab since the lease was issued.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum UserInteraction {
    /// The user has not interacted since the lease was issued.
    NoneSinceLease,
    /// The user typed, tapped, or scrolled. Any action that required an
    /// untouched tab is refused.
    ObservedSinceLease,
}

/// Whether the task, source, and domain budgets still allow the action.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum BudgetState {
    /// Budget remains.
    Remaining,
    /// A budget is exhausted. Missing limits mean policy defaults, never
    /// unlimited (domain model section 9.4).
    Exhausted,
}

/// Whether the actor lease is standing at dispatch time.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LeaseStanding {
    /// The lease is standing.
    Active,
    /// There is no standing lease: it expired, it was released, or the user
    /// took over.
    Missing,
}

/// Whether the capability still authorizes anything at dispatch time.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum CapabilityStanding {
    /// Issued, unspent, and inside its expiry.
    Valid,
    /// Past its expiry, withdrawn, or already spent. All three are reported the
    /// same way to the page-facing path, because none of them authorizes
    /// anything and the difference is only interesting in the audit record.
    NotValid,
}

/// What the broker has already decided about its own records.
///
/// Bundled rather than passed one at a time for the reason
/// [`crate::capability::IssueContext`] gives: a signature that takes three
/// loose enumerations is a signature that drifts into taking four. It also
/// gives [`Self::prepared`] a documented default, so a caller that holds no
/// prepared-effect ledger cannot forget the field into a pass.
///
/// Nothing here is observed. These are readings of the broker's own books,
/// which is exactly why they do not belong in [`ObservedState`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct BrokerStanding {
    /// Whether the actor lease is standing.
    pub lease: LeaseStanding,
    /// Whether the capability still authorizes anything.
    pub capability: CapabilityStanding,
    /// What the prepared-effect ledger said about the prepare this request
    /// commits, when it was asked.
    pub prepared: PreparedEffectStanding,
}

impl BrokerStanding {
    /// The standing of a request that commits nothing prepared.
    ///
    /// [`Self::prepared`] is [`PreparedEffectStanding::NotChecked`], which
    /// refuses a `PREPARED_EFFECT_UNCHANGED` precondition rather than passing
    /// it: a caller that did not consult a ledger has not compared anything.
    pub const fn new(lease: LeaseStanding, capability: CapabilityStanding) -> Self {
        Self {
            lease,
            capability,
            prepared: PreparedEffectStanding::NotChecked,
        }
    }

    /// The same standing, with what the prepared-effect ledger answered.
    #[must_use]
    pub const fn with_prepared(mut self, prepared: PreparedEffectStanding) -> Self {
        self.prepared = prepared;
        self
    }
}

/// The normalized navigation target of a destination-bearing node.
///
/// Comparison is exact. A destination that changed between observation and
/// dispatch fails the action closed, whatever the change was.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NormalizedDestination {
    /// The origin the navigation would land on.
    pub origin: NormalizedOrigin,
    /// The path, when policy allowed it to be observed.
    pub path: Option<String>,
    /// Whether activation is expected to open a new tab.
    pub opens_new_tab: bool,
    /// Whether activation is expected to start a download.
    pub is_download: bool,
}

/// The node as the browser sees it right now.
#[derive(Clone, Debug, PartialEq)]
pub struct ObservedNode {
    /// The identifier the endpoint resolved.
    pub node_id: SemanticNodeId,
    /// The role it currently has.
    pub role: SemanticRole,
    /// What the adapter currently believes could be attempted. This still
    /// grants nothing; it only says the action has not disappeared.
    pub available_actions: Vec<ActionType>,
    /// The states the adapter asserts. Absence of a token is not an assertion
    /// of its complement, so a check that needs `VISIBLE` needs it present.
    pub states: Vec<NodeState>,
    /// The sensitivity classification in force, already joined from every
    /// signal.
    pub sensitivity: SensitivitySet,
    /// The normalized destination, for a destination-bearing node.
    pub destination: Option<NormalizedDestination>,
    /// A digest over the current value, for the non-sensitive-value check.
    pub value_digest: Option<ContentDigest>,
    /// Who wrote this node's content, joined from every run it carries. An
    /// endpoint that does not label yields the least trusted answer, so an
    /// unlabelled node is never mistaken for a first-party one.
    pub content_trust: TrustSet,
}

impl ObservedNode {
    /// Whether the adapter asserts `state`.
    pub fn asserts(&self, state: NodeState) -> bool {
        self.states.contains(&state)
    }
}

/// What the browser observes at dispatch time.
///
/// Every field is browser-owned. A renderer may lie, so nothing here is taken
/// from a renderer-supplied string, and the checks that matter are repeated in
/// the browser broker even when the renderer already ran them.
#[derive(Clone, Debug, PartialEq)]
pub struct ObservedState {
    /// Whether the tab still exists.
    pub tab: TabPresence,
    /// Whether the frame still exists.
    pub frame: FramePresence,
    /// The browser-owned document lifecycle state.
    pub lifecycle: DocumentLifecycleState,
    /// The document instance currently in the frame.
    pub page_epoch: PageEpoch,
    /// The graph revision the endpoint can resolve at.
    pub graph_revision: GraphRevision,
    /// The committed origin of the document.
    pub origin: NormalizedOrigin,
    /// The node, or `None` when the identifier no longer resolves.
    pub node: Option<ObservedNode>,
    /// Whether the user has touched the tab since the lease was issued.
    pub user_interaction: UserInteraction,
    /// Whether budget remains.
    pub budget: BudgetState,
}
