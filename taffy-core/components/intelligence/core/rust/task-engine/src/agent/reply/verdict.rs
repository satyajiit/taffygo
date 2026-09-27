// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What becomes of each call of a reply: attempted when its turn comes, or
//! not attempted for one closed, content-free reason.

/// Why one call of a reply will not be attempted.
///
/// Closed, content-free, and enumerable. Every member is a refusal the model
/// reads back in its own transcript and re-plans against; none of them is a
/// task failure (decision 0052 section 3).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum NotAttempted {
    /// The reply was cut off at the token allowance. A truncated argument
    /// list is not a call anybody made, so no call in the reply is attempted —
    /// running the ones that happen to have parsed would execute a prefix of
    /// an intention.
    TruncatedArguments,
    /// An earlier call in the same reply was refused. The model ordered them,
    /// and a later call may depend on an earlier one having happened.
    PriorCallRefused,
    /// The name is not registered, this milestone has not reached it, it is
    /// excluded by requirement, or the task's allowlist does not admit it.
    ToolNotAvailable,
    /// The arguments do not match the compiled-in schema. Nothing is coerced
    /// and nothing is defaulted.
    ArgumentsRejected,
    /// A number the call gave names nothing this task issued, or nothing it
    /// still remembers. Those two are one situation from where the model
    /// stands — it named a number nobody printed for it — and the table
    /// cannot tell them apart either, which is
    /// [`crate::handle::MAX_RETAINED_BINDINGS`]'s whole argument.
    ///
    /// It no longer covers a number that resolves perfectly well: see
    /// [`Self::NodeHandleFromAPageTheTabLeft`] (decision 0208).
    HandleUnknown,
    /// The number resolves, and names a node read from a document its tab has
    /// since replaced.
    ///
    /// Split from [`Self::HandleUnknown`], which answered it until a phone
    /// showed what that costs: six refusals in one errand, every one on a
    /// call the model made straight after a fresh reading. The two want
    /// opposite instructions. "Use a number from the latest snapshot" is the
    /// answer to a number nobody printed, and it is worse than silence here,
    /// because the model may have done exactly that and the page moved under
    /// it — so the advice reads as "do the thing you just did", which is how
    /// a loop starts (decision 0208).
    NodeHandleFromAPageTheTabLeft,
    /// The number resolves, names a node on a page the person attached, and
    /// the call is not a pure read — while the task has a tab of its own that
    /// every such call belongs in.
    ///
    /// Numbers are task-global, so one printed for the person's page stays
    /// resolvable for the life of the task. On a phone the model, long after
    /// it had moved into its own tab, named the person's "Learn more" link in
    /// a `browser.link.open`; the node's own tab was the person's, so the
    /// person's tab was navigated away, the browser issued no source for it,
    /// and the errand's consent went with it. The person's page is read-only
    /// to the task (decisions 0224 and 0237); a read of it is still admitted.
    NodeOnThePersonsPage,
    /// `user.request_values` named a line that could take no value from a
    /// person: neither a form that shown fields belong to nor a field that can
    /// take text. The browser would draw no sheet for it (decision 0192).
    NotAField,
    /// `browser.form.fill` named a line that cannot take text: a form, or a
    /// second line the page prints for the same field without the action.
    /// The browser holds the person's values for the exact fields the sheet
    /// showed, and a fill aimed anywhere else would end that approval
    /// (decision 0195).
    NotATextField,
    /// `browser.link.open` named a line the reading did not print as a link
    /// that leads somewhere: a button, a heading, or a link with no address.
    /// The browser opens only links whose address it read, so the call would
    /// be refused a turn later as a node that is gone, and the model would
    /// read the page again and name another (decision 0241).
    NotALink,
    /// Opening a new task tab has no ownership/result executor in this build,
    /// so the operation cannot be settled or mint a later tab handle.
    TabOwnershipUnavailable,
    /// The call's raw text has no retained, single-spend residency across the
    /// policy and dispatch boundary in this build.
    OperandResidencyUnavailable,
    /// This exact call has already been refused its way to the ceiling.
    RepeatedRefusalsAbandoned,
    /// The name is registered and available and no runtime in this build
    /// serves it. Registered so the refusal is enumerable rather than a name
    /// that quietly does nothing.
    NoRuntimeHere,
    /// The turn was composed from no page and no task-owned tab, so a call
    /// that designates no node has nowhere to act.
    ///
    /// This is the loop's own fault rather than the model's, and it is a
    /// refusal anyway: the alternative is an intent carrying an empty tab
    /// identifier, which the canonical encoding refuses — and that refusal
    /// stops the whole walk rather than one call, leaving the task with no
    /// next command for the life of the process (decision 0178).
    PageUnknown,
}

impl NotAttempted {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::TruncatedArguments,
        Self::PriorCallRefused,
        Self::ToolNotAvailable,
        Self::ArgumentsRejected,
        Self::HandleUnknown,
        Self::NodeHandleFromAPageTheTabLeft,
        Self::NodeOnThePersonsPage,
        Self::NotAField,
        Self::NotATextField,
        Self::NotALink,
        Self::TabOwnershipUnavailable,
        Self::OperandResidencyUnavailable,
        Self::RepeatedRefusalsAbandoned,
        Self::NoRuntimeHere,
        Self::PageUnknown,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::TruncatedArguments => "truncated_arguments",
            Self::PriorCallRefused => "prior_call_refused",
            Self::ToolNotAvailable => "tool_not_available",
            Self::ArgumentsRejected => "arguments_rejected",
            Self::HandleUnknown => "handle_unknown",
            Self::NodeHandleFromAPageTheTabLeft => "node_handle_from_a_page_the_tab_left",
            Self::NodeOnThePersonsPage => "node_on_the_persons_page",
            Self::NotAField => "not_a_field",
            Self::NotATextField => "not_a_text_field",
            Self::NotALink => "not_a_link",
            Self::TabOwnershipUnavailable => "tab_ownership_unavailable",
            Self::OperandResidencyUnavailable => "operand_residency_unavailable",
            Self::RepeatedRefusalsAbandoned => "repeated_refusals_abandoned",
            Self::NoRuntimeHere => "no_runtime_here",
            Self::PageUnknown => "page_unknown",
        }
    }

    /// Whether this reason was reached by reading the reply rather than by
    /// asking anything else about the world.
    ///
    /// [`TurnDigest::refused_tool_calls`] counts exactly these, which is what
    /// makes the count derivable from the reply alone and therefore stable
    /// across a replay.
    ///
    /// [`TurnDigest::refused_tool_calls`]: crate::agent::turn::TurnDigest::refused_tool_calls
    pub const fn is_visible_on_the_face_of_the_reply(self) -> bool {
        !matches!(self, Self::PriorCallRefused)
    }
}

/// What the reducer will do with one call of a reply.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CallVerdict {
    /// It may be proposed, when its turn comes.
    Attemptable,
    /// It will not be attempted, for this reason.
    NotAttempted(NotAttempted),
}

impl CallVerdict {
    /// Whether the call may be proposed.
    pub const fn is_attemptable(self) -> bool {
        matches!(self, Self::Attemptable)
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Attemptable => "attemptable",
            Self::NotAttempted(reason) => reason.label(),
        }
    }
}

/// One call of a reply and what became of it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CallDisposition {
    /// Where the provider declared it, counting from zero. Stable, because a
    /// reply is frozen once it is read: this is which call of an immutable
    /// list it is, never where it landed in a batch that may be re-planned.
    pub sequence: u32,
    /// What the reducer will do with it.
    pub verdict: CallVerdict,
}
