// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The ten ordered steps of the stale-node algorithm (protocol specification
//! section 12).
//!
//! The order is load-bearing. A stale epoch has to be reported as a stale
//! epoch rather than as a missing node, because the two lead the task runtime
//! to different recoveries; a lease check has to run before a node is resolved,
//! because resolving a node for an actor that is no longer acting is work done
//! on behalf of nobody. Naming the step a decision stopped at lets an audit
//! record say which check failed without carrying anything about the page.
//!
//! Steps one to six are decisions over values. Steps seven to ten are effects,
//! and the browser broker performs them — but which effect comes next, and what
//! each of their outcomes means, is still a decision, which is why every step
//! appears here and in [`crate::sequence`].

/// One step of the section 12 sequence.
///
/// Ordered in the sequence's own order, so `>=` answers "did the sequence get
/// at least this far?" and a comparison needs no table.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum StaleNodeStep {
    /// Step 1 — resolve the tab and frame in the expected profile.
    ResolveTabAndFrame,
    /// Step 2 — confirm an active document and the exact page epoch.
    ConfirmDocumentAndEpoch,
    /// Step 3 — confirm the origin and lifecycle.
    ConfirmOrigin,
    /// Step 4 — confirm the actor lease and an unexpired, matching capability.
    ConfirmLeaseAndCapability,
    /// Step 5 — resolve the non-reused node identifier at the required graph
    /// revision.
    ResolveNode,
    /// Step 6 — re-evaluate role, actions, sensitivity, visibility, enabled and
    /// editable state, destination, and the proposal's own preconditions.
    ReevaluateNode,
    /// Step 7 — record the dispatching intent in the task journal, before any
    /// side effect.
    JournalIntent,
    /// Step 8 — dispatch through the normal browser and renderer input path.
    Dispatch,
    /// Step 9 — observe the declared postconditions until deadline,
    /// cancellation, navigation, crash, or contradiction.
    ObservePostconditions,
    /// Step 10 — record the terminal result and consume the capability.
    RecordTerminalResult,
}

impl StaleNodeStep {
    /// Every step, in the order section 12 runs them.
    pub const ALL: &'static [Self] = &[
        Self::ResolveTabAndFrame,
        Self::ConfirmDocumentAndEpoch,
        Self::ConfirmOrigin,
        Self::ConfirmLeaseAndCapability,
        Self::ResolveNode,
        Self::ReevaluateNode,
        Self::JournalIntent,
        Self::Dispatch,
        Self::ObservePostconditions,
        Self::RecordTerminalResult,
    ];

    /// The steps that are decisions over values, before anything is recorded or
    /// sent.
    pub const DECISION_STEPS: &'static [Self] = &[
        Self::ResolveTabAndFrame,
        Self::ConfirmDocumentAndEpoch,
        Self::ConfirmOrigin,
        Self::ConfirmLeaseAndCapability,
        Self::ResolveNode,
        Self::ReevaluateNode,
    ];

    /// The steps that perform or observe an effect.
    pub const EFFECT_STEPS: &'static [Self] = &[
        Self::JournalIntent,
        Self::Dispatch,
        Self::ObservePostconditions,
        Self::RecordTerminalResult,
    ];

    /// The step's number in the specification's own list, counting from one.
    pub const fn number(self) -> u8 {
        match self {
            Self::ResolveTabAndFrame => 1,
            Self::ConfirmDocumentAndEpoch => 2,
            Self::ConfirmOrigin => 3,
            Self::ConfirmLeaseAndCapability => 4,
            Self::ResolveNode => 5,
            Self::ReevaluateNode => 6,
            Self::JournalIntent => 7,
            Self::Dispatch => 8,
            Self::ObservePostconditions => 9,
            Self::RecordTerminalResult => 10,
        }
    }

    /// A short, compiled-in name, safe to record anywhere.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ResolveTabAndFrame => "resolve_tab_and_frame",
            Self::ConfirmDocumentAndEpoch => "confirm_document_and_epoch",
            Self::ConfirmOrigin => "confirm_origin",
            Self::ConfirmLeaseAndCapability => "confirm_lease_and_capability",
            Self::ResolveNode => "resolve_node",
            Self::ReevaluateNode => "reevaluate_node",
            Self::JournalIntent => "journal_intent",
            Self::Dispatch => "dispatch",
            Self::ObservePostconditions => "observe_postconditions",
            Self::RecordTerminalResult => "record_terminal_result",
        }
    }

    /// Whether the step decides rather than performs.
    ///
    /// A refusal at a decision step happened before anything reached the page,
    /// which is what makes a fresh observation and a new proposal safe after
    /// one.
    pub const fn is_decision(self) -> bool {
        self.number() <= 6
    }

    /// Whether a side effect can no longer be ruled out once this step has been
    /// reached.
    ///
    /// The journal step is the boundary: section 12 records the intent before
    /// the effect precisely so that a recovery can tell "this was about to
    /// happen" from "this did not happen".
    pub const fn may_have_reached_the_page(self) -> bool {
        !self.is_decision()
    }

    /// The step that follows this one, or `None` at the end of the sequence.
    pub const fn next(self) -> Option<Self> {
        match self {
            Self::ResolveTabAndFrame => Some(Self::ConfirmDocumentAndEpoch),
            Self::ConfirmDocumentAndEpoch => Some(Self::ConfirmOrigin),
            Self::ConfirmOrigin => Some(Self::ConfirmLeaseAndCapability),
            Self::ConfirmLeaseAndCapability => Some(Self::ResolveNode),
            Self::ResolveNode => Some(Self::ReevaluateNode),
            Self::ReevaluateNode => Some(Self::JournalIntent),
            Self::JournalIntent => Some(Self::Dispatch),
            Self::Dispatch => Some(Self::ObservePostconditions),
            Self::ObservePostconditions => Some(Self::RecordTerminalResult),
            Self::RecordTerminalResult => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::StaleNodeStep;

    #[test]
    fn the_ten_steps_are_numbered_and_ordered_the_way_the_specification_lists_them() {
        assert_eq!(StaleNodeStep::ALL.len(), 10);
        let mut expected = 1_u8;
        let mut previous: Option<StaleNodeStep> = None;
        for step in StaleNodeStep::ALL {
            assert_eq!(step.number(), expected, "{}", step.label());
            if let Some(previous) = previous {
                assert!(previous < *step);
                assert_eq!(previous.next(), Some(*step));
            }
            previous = Some(*step);
            expected = expected.saturating_add(1);
        }
        assert_eq!(StaleNodeStep::RecordTerminalResult.next(), None);
    }

    #[test]
    fn the_journal_step_is_where_a_side_effect_stops_being_impossible() {
        for step in StaleNodeStep::DECISION_STEPS {
            assert!(step.is_decision());
            assert!(!step.may_have_reached_the_page());
        }
        for step in StaleNodeStep::EFFECT_STEPS {
            assert!(!step.is_decision());
            assert!(step.may_have_reached_the_page());
        }
        assert_eq!(
            StaleNodeStep::DECISION_STEPS.len() + StaleNodeStep::EFFECT_STEPS.len(),
            StaleNodeStep::ALL.len()
        );
    }

    #[test]
    fn every_step_has_a_distinct_compiled_in_name() {
        let mut labels: Vec<&str> = StaleNodeStep::ALL.iter().map(|step| step.label()).collect();
        labels.sort_unstable();
        let count = labels.len();
        labels.dedup();
        assert_eq!(labels.len(), count);
    }
}
