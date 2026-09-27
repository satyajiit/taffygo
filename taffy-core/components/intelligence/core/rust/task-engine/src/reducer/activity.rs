// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Where the task's own record of what it did is written (decision 0148).
//!
//! One module for the writes, beside the handlers that make them, because the
//! rule that matters about them is a rule about *where*: a step is appended in
//! the handler that commits the transition it is about, and nowhere else. A
//! step appended in a port, a projection or the browser is a step a restored
//! task does not have, and a timeline that empties when a person reopens the
//! browser is worse than one that was never there.
//!
//! What each step *says* is not here and is not anywhere in this crate. A
//! sentence composed in the core is a sentence that cannot be translated
//! (parity row PAR-L10N-001) and a wire field that could carry page text. The
//! kind, the host and the count are the whole of what crosses.

use bip_types::identity::TabId;
use bip_types::ActionResultCode;

use super::Reducer;
use crate::ids::IdSource;
use crate::task::{host_of_origin, TaskActivityKind};
use crate::time::Clock;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Appends one step to what this task did.
    ///
    /// The clock is the reducer's own, so a replay stamps each step with the
    /// same reading the run did — the journal carries the time, and this reads
    /// it from the same place `updated_at` does.
    pub(super) fn note(&mut self, kind: TaskActivityKind, host: Option<String>, count: u32) {
        let at = self.clock.now_utc();
        self.task.activity.append(kind, host, count, at);
    }

    /// The host a step about this tab should name, if the task knows one.
    ///
    /// A consented source is the only thing that binds a tab to an origin here,
    /// and that is the right source: the task may only ever name a host it was
    /// consented to. A tab it has no source for gets no host rather than a
    /// guess, and the sentence composed on the surface is the one without it.
    pub(super) fn host_of_tab(&self, tab: &TabId) -> Option<String> {
        self.task
            .consented_sources
            .iter()
            .find(|source| &source.tab_id == tab)
            .and_then(|source| host_of_origin(&source.normalized_origin))
    }
}

/// Which of the two refusal steps a terminal result code is.
///
/// The split a person cares about is "the page was not there" against "Taffy
/// tried something and it did not work", because the first is nobody's fault
/// and the second is worth reading twice. Exhaustive on purpose: a new result
/// code is a decision about which of these it is, and a decision is better made
/// at a compile error than by a catch-all that quietly calls it a refusal.
pub(super) const fn refusal_step(code: ActionResultCode) -> TaskActivityKind {
    use ActionResultCode as Code;
    match code {
        // The page, the frame or the document the move was for is not there
        // any more — or never finished arriving. Nothing was refused; there
        // was nothing to refuse it.
        Code::TabGone
        | Code::FrameGone
        | Code::DocumentInactive
        | Code::StalePageEpoch
        | Code::RendererCrashed
        | Code::CancelledByNavigation
        | Code::PostconditionTimeout => TaskActivityKind::PageUnavailable,
        // Everything else: a policy said no, a target moved or was never
        // legitimate, a budget ran out, the runtime could not carry it, or the
        // person stopped it. All of them are a move that did not happen, and
        // the sentence for them says Taffy tried another way.
        Code::Verified
        | Code::DeniedByPolicy
        | Code::ApprovalRequired
        | Code::ApprovalDenied
        | Code::ActorLeaseMissing
        | Code::CapabilityExpired
        | Code::StaleGraph
        | Code::NodeGone
        | Code::OriginChanged
        | Code::RoleOrActionChanged
        | Code::NotVisible
        | Code::Occluded
        | Code::NotEnabled
        | Code::NotEditable
        | Code::SensitiveField
        | Code::DestinationChanged
        | Code::Unsupported
        | Code::BudgetExceeded
        | Code::DispatchFailed
        | Code::NavigationStarted
        | Code::PostconditionFailed
        | Code::CancelledByUser
        | Code::OutcomeUnknown
        | Code::InternalError
        | Code::EgressNotAuthorized
        | Code::DestinationClassRestricted
        | Code::UntrustedContentOrigin
        | Code::PreparedEffectChanged
        | Code::CommitWithoutPrepare
        | Code::GraphMovedDuringPreflight
        | Code::ValueReferenceUnknown => TaskActivityKind::MoveRefused,
    }
}
