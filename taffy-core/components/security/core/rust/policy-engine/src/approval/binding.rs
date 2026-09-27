// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What an approval sheet showed, and what makes a shown answer stop applying
//! (domain model section 12.5, threat model section 18.4).
//!
//! The comparison is exact in both directions. A binding that looks *safer*
//! than the approved one invalidates the approval too: a lowered risk or a
//! narrowed data class is still not the thing the person was shown, and
//! accepting it would make the sheet's contents advisory.
//!
//! Nothing here holds page text. The sentences a person reads are composed from
//! trusted local templates in the browser user interface, so a page or a model
//! cannot supply what somebody approves.

use bip_types::identity::{ContentDigest, FrameId, PageEpoch, SemanticNodeId, TabId};
use bip_types::sensitivity::SensitivitySet;

use crate::action_class::ActionClass;
use crate::origin::NormalizedOrigin;
use crate::phase::ActionPhase;
use crate::risk::RiskClass;

/// Which part of the approved question moved.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ApprovalInvalidation {
    /// The proposal itself is a different one.
    ActionChanged,
    /// The sheet answered one half of a two-step authorization and the request
    /// is the other half (decision 0022).
    ///
    /// Second, immediately after the action comparison, and its own reason
    /// rather than a changed class. Reporting "the sheet said prepare and the
    /// request says submit" as a changed action class would be false: the class
    /// is identical and the meaning is opposite. It is also what refuses a
    /// cheap prepare's confirmation spliced onto the expensive commit of the
    /// same action.
    PhaseChanged,
    /// The class of effect changed.
    ActionClassChanged,
    /// The tab, frame, or node is not the approved target.
    TargetChanged,
    /// The document instance changed under the approval.
    PageStateChanged,
    /// The document is no longer on the approved origin.
    OriginChanged,
    /// The navigation would land somewhere else.
    DestinationChanged,
    /// The data classes disclosed are not the ones shown.
    DataClassesChanged,
    /// The effective risk is not the one shown.
    RiskChanged,
}

impl ApprovalInvalidation {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::ActionChanged,
        Self::PhaseChanged,
        Self::ActionClassChanged,
        Self::TargetChanged,
        Self::PageStateChanged,
        Self::OriginChanged,
        Self::DestinationChanged,
        Self::DataClassesChanged,
        Self::RiskChanged,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ActionChanged => "action_changed",
            Self::PhaseChanged => "phase_changed",
            Self::ActionClassChanged => "action_class_changed",
            Self::TargetChanged => "target_changed",
            Self::PageStateChanged => "page_state_changed",
            Self::OriginChanged => "origin_changed",
            Self::DestinationChanged => "destination_changed",
            Self::DataClassesChanged => "data_classes_changed",
            Self::RiskChanged => "risk_changed",
        }
    }
}

/// Everything the approval sheet showed, as values.
///
/// Compared by [`Self::invalidation_against`] rather than by `PartialEq`, so a
/// caller reads *which* part moved instead of a bare "different". The digest it
/// carries is a protocol value without an `Eq` implementation, which is why the
/// comparison is written out.
#[derive(Clone, Debug, PartialEq)]
pub struct ApprovalBinding {
    /// What the effect does.
    pub action_class: ActionClass,
    /// Which half of a two-step authorization the sheet asked about. A prepare
    /// and the commit it stages are the same class and opposite questions.
    pub phase: ActionPhase,
    /// The digest of the exact proposal.
    pub action_digest: ContentDigest,
    /// The tab it happens in.
    pub tab_id: TabId,
    /// The frame it happens in.
    pub frame_id: FrameId,
    /// The document instance it happens in.
    pub page_epoch: PageEpoch,
    /// The origin the document is on.
    pub origin: NormalizedOrigin,
    /// The node, for a node-targeted class.
    pub node_id: Option<SemanticNodeId>,
    /// Where a navigation would land, for a destination-bearing action.
    pub destination: Option<NormalizedOrigin>,
    /// The data classes the action discloses.
    pub data_classes: SensitivitySet,
    /// The effective risk shown on the sheet.
    pub risk: RiskClass,
}

impl ApprovalBinding {
    /// Which part of this binding `current` disagrees with, if any.
    ///
    /// The order is the order the sheet reads in, so the reason recorded is the
    /// first thing a person would notice had changed.
    pub fn invalidation_against(&self, current: &Self) -> Option<ApprovalInvalidation> {
        if self.action_digest.algorithm != current.action_digest.algorithm
            || self.action_digest.value != current.action_digest.value
        {
            return Some(ApprovalInvalidation::ActionChanged);
        }
        if self.phase != current.phase {
            return Some(ApprovalInvalidation::PhaseChanged);
        }
        if self.action_class != current.action_class {
            return Some(ApprovalInvalidation::ActionClassChanged);
        }
        if self.tab_id != current.tab_id
            || self.frame_id != current.frame_id
            || self.node_id != current.node_id
        {
            return Some(ApprovalInvalidation::TargetChanged);
        }
        if self.page_epoch != current.page_epoch {
            return Some(ApprovalInvalidation::PageStateChanged);
        }
        if !self.origin.is_same_origin(&current.origin) {
            return Some(ApprovalInvalidation::OriginChanged);
        }
        if !destinations_match(self.destination.as_ref(), current.destination.as_ref()) {
            return Some(ApprovalInvalidation::DestinationChanged);
        }
        if self.data_classes != current.data_classes {
            return Some(ApprovalInvalidation::DataClassesChanged);
        }
        if self.risk != current.risk {
            return Some(ApprovalInvalidation::RiskChanged);
        }
        None
    }
}

/// Whether two optional destinations are the same one.
///
/// A destination that appeared where there was none, or disappeared where there
/// was one, is a change: the sheet either promised a navigation or promised
/// none.
fn destinations_match(
    approved: Option<&NormalizedOrigin>,
    current: Option<&NormalizedOrigin>,
) -> bool {
    match (approved, current) {
        (None, None) => true,
        (Some(approved), Some(current)) => approved.is_same_origin(current),
        _ => false,
    }
}
