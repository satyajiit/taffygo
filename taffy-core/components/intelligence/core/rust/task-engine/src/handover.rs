// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Handing the page back to the person, and taking it back afterwards.
//!
//! # What a handover is
//!
//! `user.handover` is an ordinary registered row (decision 0054 section 6),
//! and this module is what the reducer does when the assistant calls it. The
//! task stops, authority is revoked, and the person acts in their own tab
//! under their own attribution. Nothing here watches what they do, and nothing
//! here reads the page while they are doing it.
//!
//! # What is deliberately absent
//!
//! **There is no field naming the challenge, and handover is the floor.**
//!
//! This module used to argue that the product recognises no challenge at all.
//! Decision 0088 changed that: the renderer now classifies a challenge kind
//! beside the sensitivity it already classifies, so that a surface can offer
//! the person something better than a bare page — the image in a sheet, a
//! numeric keypad, a highlight drawn around the widget.
//!
//! What did **not** change is anything here, and the reason is the shape of
//! that classification rather than its accuracy. It is a *hint*, and a hint may
//! only ever add a surface. It cannot cause a handover not to happen. Every
//! path that ends here still ends here: `user.handover` stays in
//! [`crate::tool::UNCONDITIONAL_TOOLS`], no narrowing removes it, and
//! [`crate::authority::ActionClass::BypassAccessControl`] and
//! [`crate::authority::ActionClass::ExtractCredential`] are still prohibited by
//! class, so the challenge, the code and the password are still refused before
//! anything looks at them.
//!
//! That is what keeps the original argument true where it was load-bearing.
//! "Refusal by exhaustion has no false negatives; a detector does" was a
//! statement about what a *wrong* detection would cost. Under the old design a
//! false negative meant the product concluding no handover was needed and
//! pressing on, which is unrecoverable. Under this one it means the nicer
//! surface is not offered and the errand falls back through exactly the
//! exhaustion path below — the person ends up where they would have been
//! anyway. A false positive costs them a sheet they can dismiss.
//!
//! So a `challenge_kind` field on *this* record would still be a detector
//! wearing a record's clothes, and there is still not one. What the assistant
//! may say is why *it* stopped, from the closed list in [`crate::tool`]'s
//! handover parameters, and that is a statement about the assistant rather
//! than about the page.
//!
//! # The three facts the reducer keeps
//!
//! An identity, so a completion cannot answer a handover that is not the one
//! open. The two lease identities the browser reports at the end, so the audit
//! can tell what the assistant did from what the person did. And a bounded
//! count of the input the browser observed, which is the only evidence that
//! the person acted at all. [`PersonInput`] holds the whole of the decision
//! about what that count may say.

use core::fmt;

use crate::authority::ActorLeaseId;

/// Maximum opaque handover identity accepted by the reducer.
pub const MAX_HANDOVER_ID_BYTES: usize = 256;

/// How long a handover window stays open, in milliseconds.
///
/// A duration rather than an instant, because this reducer reads no clock:
/// [`crate::effect::Effect::AwaitHandover`] carries the number and the browser
/// adds it to the moment it opens the surface. Keeping the number here makes
/// the window a portable decision that a replay can re-derive, rather than a
/// browser detail that would differ between a phone and a test.
///
/// Five minutes is long enough to read a challenge, fetch a code from another
/// application and come back, and short enough that a task nobody returned to
/// stops holding a tab open indefinitely. Expiry is not a failure: it holds
/// the task, and the person resumes it whenever they like.
pub const HANDOVER_WINDOW_MS: u32 = 300_000;

/// One handover's identity.
///
/// Derived by the agent loop from the turn and call that asked for it, never
/// minted, for the same reason [`crate::ids::ModelCallId`] is derived: a
/// replay has to reach the same identity, so a completion recorded before a
/// restart still answers the handover the rebuild re-opens.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct HandoverId(String);

impl HandoverId {
    /// Accepts one non-empty bounded opaque identity.
    pub fn new(value: impl Into<String>) -> Result<Self, HandoverFactError> {
        let value = value.into();
        if value.is_empty() || value.len() > MAX_HANDOVER_ID_BYTES {
            return Err(HandoverFactError::InvalidHandoverId);
        }
        Ok(Self(value))
    }

    /// The opaque identity, for exact correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for HandoverId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// The identity of the handover the `sequence`-th call of turn `ordinal` asks
/// for.
///
/// The same two numbers [`crate::agent`] already uses to name a call, and for
/// the same reason: a turn ordinal only ever increases and a reply is frozen
/// the moment it is read, so the *n*-th call of the *m*-th turn of one task
/// happens exactly once. Neither number is a position in a batch that could be
/// re-planned — an identifier must name what it is about, never where it
/// landed in the batch that produced it.
///
/// Total: the format cannot produce an empty or over-long string, so the
/// constructor cannot refuse it.
pub fn handover_id_for_call(ordinal: u64, sequence: u32) -> HandoverId {
    HandoverId(format!("turn-{ordinal}-handover-{sequence}"))
}

/// How much input the browser observed in the handed-over tab.
///
/// # The question this answers, and the one it refuses to
///
/// A completed handover carries a claim: the person acted. That claim needs
/// evidence that is neither the assistant's word nor the page's, and only the
/// browser process has it, because that is where operating-system input
/// arrives. `ActorLeaseRegistry` counts it there — see
/// `taffy-core/components/security/browser/action_authority.h` — and the
/// number travels back inside [`HandoverCompletion`].
///
/// # What counts
///
/// **Counted:** one discrete committed input the person produced — a key
/// press, or a pointer or touch commit.
///
/// **Not counted:** movement, wheel, scroll, hover, pinch and fling. Looking
/// at a challenge is not answering one, and a page that scrolls itself would
/// otherwise manufacture the evidence that a person was there.
///
/// **Not counted:** anything outside the open handover window, and anything in
/// a tab other than the handed-over one. A count that could be accumulated
/// before the window opened would be a count the page's own timing controls.
///
/// # Why it saturates, and why keys and taps share one counter
///
/// A person typing a one-time code or a password into a handed-over tab
/// produces exactly as many key events as the secret has characters. An exact
/// per-kind count is therefore *the length of the secret*, written into a
/// durable record by the one part of this product that undertook never to see
/// it. So the counter does two things. It adds every counted kind into a
/// single number, which makes six keystrokes indistinguishable from two taps
/// and four keystrokes. And it stops at [`PersonInput::CEILING`], which makes
/// every secret long enough to be worth protecting indistinguishable from
/// every other.
///
/// What survives is what an audit needs: nothing happened, one thing happened,
/// or the person worked in the page. What does not survive is how long what
/// they typed was.
///
/// # It is evidence, never a gate
///
/// Nothing decides from this number that a challenge was solved. A completed
/// handover with a zero count is recorded and still completes: the person may
/// have answered on another device, or found that nothing was needed. Deciding
/// from the count would be a detector reintroduced through the back door, and
/// not building one is the reason handover exists at all.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct PersonInput(u8);

impl PersonInput {
    /// The value the count stops at.
    ///
    /// Three, so the record can distinguish "nobody touched the page" from
    /// "one stray tap" from "the person worked here", and can distinguish
    /// nothing beyond that. A larger ceiling would start to recover the length
    /// of short secrets; a smaller one would merge the stray tap with the
    /// work.
    pub const CEILING: u8 = 3;

    /// No input at all.
    pub const NONE: Self = Self(0);

    /// The evidence for `observed` counted events, clamped to the ceiling.
    ///
    /// Takes a `u32` and saturates rather than taking a `u8` and trusting the
    /// caller to have clamped: the browser's counter is the one that saturates
    /// first, and this is the second place that refuses to carry a number
    /// larger than the ceiling. Two independent clamps cost nothing and remove
    /// the case where one of them is changed alone.
    ///
    /// Clamp first, then narrow. The narrowing cannot fail after the clamp,
    /// and its fallback is the ceiling rather than zero, so a change to one
    /// side alone can only over-report that a person acted — never turn "the
    /// person worked here" into "nobody did".
    pub fn observed(observed: u32) -> Self {
        let clamped = observed.min(u32::from(Self::CEILING));
        Self(u8::try_from(clamped).unwrap_or(Self::CEILING))
    }

    /// The clamped count.
    pub const fn count(self) -> u8 {
        self.0
    }

    /// Whether the browser observed the person do anything at all.
    pub const fn any(self) -> bool {
        self.0 > 0
    }
}

/// What the browser reports when a handover ends because the person came back.
///
/// # Why both lease identities are here
///
/// The assistant held a lease over the tab; the handover revoked it; resuming
/// mints another. The audit has to be able to say which actions were the
/// assistant's and which were the person's, and it can only do that if the two
/// stretches of assistant activity are under different lease identities. So
/// the completion carries both, [`HandoverCompletion::resumption_lease_is_new`]
/// is the property that matters, and [`crate::transition::Guard::ResumptionLeaseIsNew`]
/// refuses a completion that does not have it.
///
/// The browser is the authority on both values and this reducer does not
/// police it — a browser that reported two invented identities would be
/// believed. What the refusal removes is the failure that would otherwise be
/// easy and silent: a resumption that reuses the lease it already had, which
/// leaves an audit unable to draw the line at all.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HandoverCompletion {
    handover_id: HandoverId,
    lease_before: ActorLeaseId,
    resumed_with: ActorLeaseId,
    person_input: PersonInput,
}

impl HandoverCompletion {
    /// Accepts one completion, refusing a malformed lease identity.
    ///
    /// A *reused* identity is well formed and is not refused here: it is a
    /// claim that is wrong rather than a value that is broken, and the reducer
    /// refuses claims through the transition table so the refusal reaches the
    /// journal with a name. A constructor error would be invisible to the
    /// journal, which is the one place a resumption under the assistant's old
    /// lease needs to be visible.
    pub fn new(
        handover_id: HandoverId,
        lease_before: ActorLeaseId,
        resumed_with: ActorLeaseId,
        person_input: PersonInput,
    ) -> Result<Self, HandoverFactError> {
        if lease_before.as_str().is_empty() || resumed_with.as_str().is_empty() {
            return Err(HandoverFactError::InvalidLeaseId);
        }
        Ok(Self {
            handover_id,
            lease_before,
            resumed_with,
            person_input,
        })
    }

    /// Which handover this ends.
    pub const fn handover_id(&self) -> &HandoverId {
        &self.handover_id
    }

    /// The lease the handover revoked.
    pub const fn lease_before(&self) -> &ActorLeaseId {
        &self.lease_before
    }

    /// The lease the assistant resumes under.
    pub const fn resumed_with(&self) -> &ActorLeaseId {
        &self.resumed_with
    }

    /// What the browser observed the person do.
    pub const fn person_input(&self) -> PersonInput {
        self.person_input
    }

    /// Whether the resumption acquired a lease identity of its own.
    pub fn resumption_lease_is_new(&self) -> bool {
        self.resumed_with != self.lease_before
    }
}

/// A handover fact was malformed at the reducer boundary.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoverFactError {
    /// The identity was empty or longer than [`MAX_HANDOVER_ID_BYTES`].
    InvalidHandoverId,
    /// A lease identity was empty.
    InvalidLeaseId,
}

#[cfg(test)]
mod tests {
    use super::{
        handover_id_for_call, HandoverCompletion, HandoverFactError, HandoverId, PersonInput,
        MAX_HANDOVER_ID_BYTES,
    };
    use crate::authority::ActorLeaseId;

    #[test]
    fn handover_identity_is_non_empty_and_bounded() {
        assert_eq!(
            HandoverId::new(""),
            Err(HandoverFactError::InvalidHandoverId)
        );
        assert!(HandoverId::new("h".repeat(MAX_HANDOVER_ID_BYTES)).is_ok());
        assert_eq!(
            HandoverId::new("h".repeat(MAX_HANDOVER_ID_BYTES + 1)),
            Err(HandoverFactError::InvalidHandoverId)
        );
    }

    #[test]
    fn a_derived_identity_names_the_call_and_not_a_position_in_a_batch() {
        assert_eq!(
            handover_id_for_call(4, 2).as_str(),
            "turn-4-handover-2",
            "the identity is the turn and the call, both of which happen once"
        );
        assert_ne!(handover_id_for_call(4, 2), handover_id_for_call(5, 2));
        assert_ne!(handover_id_for_call(4, 2), handover_id_for_call(4, 3));
    }

    #[test]
    fn the_input_count_stops_at_the_ceiling() {
        assert_eq!(PersonInput::observed(0), PersonInput::NONE);
        assert!(!PersonInput::NONE.any());
        assert_eq!(PersonInput::observed(1).count(), 1);
        assert!(PersonInput::observed(1).any());
        assert_eq!(
            PersonInput::observed(PersonInput::CEILING.into()).count(),
            PersonInput::CEILING
        );
    }

    /// The privacy property, stated as a test rather than only as prose: a
    /// six-character secret and a four-character one are the same record.
    #[test]
    fn a_long_input_run_cannot_be_told_from_a_longer_one() {
        assert_eq!(PersonInput::observed(4), PersonInput::observed(6));
        assert_eq!(PersonInput::observed(6), PersonInput::observed(u32::MAX));
    }

    #[test]
    fn a_completion_refuses_an_empty_lease_identity() {
        let identity = handover_id_for_call(0, 0);
        assert_eq!(
            HandoverCompletion::new(
                identity.clone(),
                ActorLeaseId::new(""),
                ActorLeaseId::new("lease-2"),
                PersonInput::NONE,
            ),
            Err(HandoverFactError::InvalidLeaseId)
        );
        assert_eq!(
            HandoverCompletion::new(
                identity,
                ActorLeaseId::new("lease-1"),
                ActorLeaseId::new(""),
                PersonInput::NONE,
            ),
            Err(HandoverFactError::InvalidLeaseId)
        );
    }

    #[test]
    fn a_reused_lease_is_well_formed_and_is_left_for_the_guard_to_refuse() {
        let completion = HandoverCompletion::new(
            handover_id_for_call(0, 0),
            ActorLeaseId::new("lease-1"),
            ActorLeaseId::new("lease-1"),
            PersonInput::observed(2),
        )
        .expect("a reused lease is a well-formed value");
        assert!(!completion.resumption_lease_is_new());
    }
}
