// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One bounded continuation of what a person is typing (decision 0097).
//!
//! A suggestion is not work a person delegated, so none of the machinery that
//! makes work accountable applies to it: no task is opened, nothing is
//! journalled, nothing is audited, and nothing survives the process. That is
//! the decision rather than an omission — a record of every half-typed
//! sentence would be a keystroke log with a retention policy.
//!
//! This protocol owns the part the composition does not: the single flight,
//! the effect identity, and the rule that a newer request supersedes an older
//! one. Supersession is the whole reason a request carries an identity. A
//! person types faster than a provider answers, so an answer to the sentence
//! they were writing two keystrokes ago is not a late answer, it is the wrong
//! one, and it has to be discarded rather than shown.

use task_engine::{BrowserSessionId, MAX_BROWSER_SESSION_ID_BYTES};

use crate::{wire, ServiceGeneration};

const COMPLETION_EFFECT_PREFIX: &str = "composer-completion";
const MAX_GENERATION_DIGITS: usize = 20;
const MAX_ATTEMPT_DIGITS: usize = 10;

/// Longest live composer identity the protocol can mint.
pub const MAX_COMPLETION_EFFECT_ID_BYTES: usize = COMPLETION_EFFECT_PREFIX.len()
    + 1
    + MAX_BROWSER_SESSION_ID_BYTES
    + 1
    + MAX_GENERATION_DIGITS
    + 1
    + MAX_ATTEMPT_DIGITS;

const _: () = assert!(MAX_COMPLETION_EFFECT_ID_BYTES <= wire::MAX_IDENTIFIER_BYTES);

/// The answer allowance a suggestion asks for, in tokens.
///
/// Small deliberately. A suggestion finishes a phrase; a suggestion that
/// writes a paragraph is one a person has to read before rejecting, which
/// costs them more than typing it themselves would have.
pub const COMPLETION_ANSWER_TOKENS: u64 = 64;

/// The completion size cap the effect carries, in bytes.
///
/// The boundary contract's own bound on what may be handed to a surface, so
/// the core cannot produce a suggestion the observer push would refuse. The
/// contract states it as a count and the effect field is a `u32`, so the
/// conversion is checked here once rather than being cast at each use: a bound
/// that did not fit would be a bound this build could not honour, and it
/// should stop the build rather than wrap.
pub const COMPLETION_MAX_OUTPUT_BYTES: u32 = {
    assert!(wire::MAX_COMPOSER_COMPLETION_BYTES <= u32::MAX as usize);
    #[allow(clippy::cast_possible_truncation)]
    {
        wire::MAX_COMPOSER_COMPLETION_BYTES as u32
    }
};

/// The one request in flight, if there is one.
#[derive(Clone, Debug, PartialEq, Eq)]
struct InFlight {
    request_id: String,
    effect_id: String,
}

/// The composer's single-flight discipline.
#[derive(Clone, Debug)]
pub struct CompletionProtocol {
    in_flight: Option<InFlight>,
    incarnation: String,
    minted: u32,
}

/// What starting a request displaced.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CompletionFlight {
    /// The identity the effect is dispatched under.
    pub effect_id: String,
    /// The effect a newer request displaced, if one was running.
    ///
    /// The caller cancels it. It is named rather than forgotten because an
    /// effect nobody stops is an effect that still bills.
    pub superseded_effect_id: Option<String>,
}

/// Why no new live composer identity could be minted.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompletionRefusal {
    /// This incarnation has used every representable attempt ordinal.
    IdentityExhausted,
}

impl CompletionProtocol {
    /// Binds suggestion flights to one browser/service incarnation.
    ///
    /// There is deliberately no `Default`: a counter with no incarnation
    /// remints `composer-completion-1` after every restart, which can let a
    /// late terminal match another process's live flight.
    #[must_use]
    pub fn new(browser_session_id: &BrowserSessionId, generation: ServiceGeneration) -> Self {
        Self {
            in_flight: None,
            incarnation: format!("{}-{}", browser_session_id.as_str(), generation.value()),
            minted: 0,
        }
    }

    /// Claims the flight for one request, displacing any older one.
    ///
    /// The surface request identity is not authority, so the live identity is
    /// an incarnation plus an ordinal owned here. It is never journalled.
    pub fn begin(&mut self, request_id: &str) -> Result<CompletionFlight, CompletionRefusal> {
        self.minted = self
            .minted
            .checked_add(1)
            .ok_or(CompletionRefusal::IdentityExhausted)?;
        let effect_id = format!(
            "{COMPLETION_EFFECT_PREFIX}-{}-{}",
            self.incarnation, self.minted
        );
        let superseded = self
            .in_flight
            .replace(InFlight {
                request_id: request_id.to_owned(),
                effect_id: effect_id.clone(),
            })
            .map(|held| held.effect_id);
        Ok(CompletionFlight {
            effect_id,
            superseded_effect_id: superseded,
        })
    }

    /// Releases a flight whose effect was never dispatched.
    ///
    /// Only correct while the browser has not seen the effect. Composition
    /// calls it when body writing refuses after the flight was claimed, so
    /// nothing is in flight anywhere.
    pub fn abandon(&mut self, effect_id: &str) {
        if self
            .in_flight
            .as_ref()
            .is_some_and(|held| held.effect_id == effect_id)
        {
            self.in_flight = None;
        }
    }

    /// The request one delivered effect answers, if it is still the awaited one.
    ///
    /// `None` means the answer arrived for a request the person has already
    /// typed past. It is dropped rather than delivered: showing it would put
    /// a suggestion for one sentence under a different one.
    pub fn settle(&mut self, effect_id: &str) -> Option<String> {
        let held = self.in_flight.as_ref()?;
        if held.effect_id != effect_id {
            return None;
        }
        let request_id = held.request_id.clone();
        self.in_flight = None;
        Some(request_id)
    }

    /// Releases a flight held for one request identity.
    ///
    /// Keyed on the request rather than on an effect, because the terminal
    /// this answers is the *push*, and a push names the request it carries
    /// rather than the model call that produced it. By the time one arrives
    /// the flight has normally been settled already, by the model terminal
    /// that composed it — so this is the guard rather than the ordinary path.
    /// It is still worth having: the push is the last thing the core hears
    /// about a request, and a flight left running past it is one the composer
    /// would go on waiting for, with no way to ask again for the sentence the
    /// person has already finished typing.
    ///
    /// A request identity that is not the one held releases nothing, for the
    /// same reason [`Self::abandon`] checks before it clears: a slow answer to
    /// an older request must not cancel the newer one that replaced it.
    pub fn release(&mut self, request_id: &str) {
        let _withdrawn = self.withdraw(request_id);
    }

    /// Releases the flight held for one request and names the dispatch to stop.
    ///
    /// The difference from [`Self::release`] is the return value, and it is the
    /// whole of decision 0097 section 3: a superseded request is *cancelled*
    /// rather than awaited, and cancelling one means telling the browser which
    /// effect to withdraw. Without a name for it a surface could only mint a
    /// newer identity and discard the stale answer, which pays for a model call
    /// per abandoned request — and the caret work made that worse, because
    /// moving the caret supersedes too.
    ///
    /// `None` is ordinary rather than an error: the request may already have
    /// been answered, or a newer one may have displaced it, and in both cases
    /// there is nothing left to stop.
    pub fn withdraw(&mut self, request_id: &str) -> Option<String> {
        let held = self.in_flight.as_ref()?;
        if held.request_id != request_id {
            return None;
        }
        let effect_id = held.effect_id.clone();
        self.in_flight = None;
        Some(effect_id)
    }

    /// Whether a request is running.
    #[must_use]
    pub fn is_in_flight(&self) -> bool {
        self.in_flight.is_some()
    }
}

/// The suggestion carried in a completion body, or nothing.
///
/// A model asked to continue a sentence answers with prose, and prose that is
/// only whitespace is not a suggestion. Absent is said rather than an empty
/// string, so a surface can stop waiting instead of rendering nothing and
/// still showing a spinner.
#[must_use]
pub fn suggestion_from(completion: &[u8]) -> Option<String> {
    let text = core::str::from_utf8(completion).ok()?;
    let trimmed = text.trim_end();
    if trimmed.trim().is_empty() {
        return None;
    }
    // Bounded here as well as at the boundary, because what a provider
    // returned is the provider's and the contract's bound is this product's.
    let mut kept = String::new();
    for character in trimmed.chars() {
        if kept.len() + character.len_utf8() > wire::MAX_COMPOSER_COMPLETION_BYTES {
            break;
        }
        kept.push(character);
    }
    if kept.trim().is_empty() {
        None
    } else {
        Some(kept)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn protocol(session: &str, generation: u64) -> CompletionProtocol {
        let session = BrowserSessionId::new(session).unwrap();
        CompletionProtocol::new(&session, ServiceGeneration::new(generation))
    }

    fn begin(protocol: &mut CompletionProtocol, request_id: &str) -> CompletionFlight {
        protocol.begin(request_id).unwrap()
    }

    #[test]
    fn a_newer_request_supersedes_the_one_running() {
        let mut protocol = protocol("browser-1", 1);
        let first = begin(&mut protocol, "request-1");
        assert_eq!(first.superseded_effect_id, None);
        let second = begin(&mut protocol, "request-2");
        assert_eq!(
            second.superseded_effect_id.as_deref(),
            Some(first.effect_id.as_str()),
            "the displaced effect is named so the caller can stop it"
        );
        assert_ne!(first.effect_id, second.effect_id);
    }

    #[test]
    fn an_answer_to_a_superseded_request_is_dropped() {
        let mut protocol = protocol("browser-1", 1);
        let first = begin(&mut protocol, "request-1");
        let second = begin(&mut protocol, "request-2");
        assert_eq!(
            protocol.settle(&first.effect_id),
            None,
            "the person has typed past it"
        );
        assert_eq!(
            protocol.settle(&second.effect_id).as_deref(),
            Some("request-2")
        );
        assert!(!protocol.is_in_flight());
    }

    #[test]
    fn settling_twice_answers_once() {
        let mut protocol = protocol("browser-1", 1);
        let flight = begin(&mut protocol, "request-1");
        assert!(protocol.settle(&flight.effect_id).is_some());
        assert_eq!(protocol.settle(&flight.effect_id), None);
    }

    #[test]
    fn abandoning_releases_only_the_flight_it_names() {
        let mut protocol = protocol("browser-1", 1);
        let first = begin(&mut protocol, "request-1");
        let second = begin(&mut protocol, "request-2");
        protocol.abandon(&first.effect_id);
        assert!(
            protocol.is_in_flight(),
            "a stale identity releases nothing, or a slow first request would cancel the second"
        );
        protocol.abandon(&second.effect_id);
        assert!(!protocol.is_in_flight());
    }

    #[test]
    fn a_delivered_push_releases_only_the_request_it_names() {
        let mut protocol = protocol("browser-1", 1);
        begin(&mut protocol, "request-1");
        protocol.release("request-2");
        assert!(
            protocol.is_in_flight(),
            "a push for a request the person has typed past releases nothing"
        );
        protocol.release("request-1");
        assert!(!protocol.is_in_flight());
    }

    #[test]
    fn an_empty_or_blank_answer_is_no_suggestion() {
        assert_eq!(suggestion_from(b""), None);
        assert_eq!(suggestion_from(b"   \n\t "), None);
        assert_eq!(
            suggestion_from(b" is cheaper.  \n"),
            Some(" is cheaper.".to_owned()),
            "trailing whitespace is dropped and leading whitespace is not, because a continuation joins the text before it"
        );
    }

    #[test]
    fn two_browser_sessions_cannot_mint_the_same_live_flight() {
        let first = begin(&mut protocol("browser-1", 1), "request-1").effect_id;
        let other_session = begin(&mut protocol("browser-2", 1), "request-1").effect_id;
        let other_generation = begin(&mut protocol("browser-1", 2), "request-1").effect_id;
        assert_ne!(first, other_session);
        assert_ne!(first, other_generation);
    }

    #[test]
    fn an_exhausted_counter_refuses_instead_of_repeating_an_identity() {
        let mut protocol = protocol("browser-1", 1);
        protocol.minted = u32::MAX;
        assert_eq!(
            protocol.begin("request-1"),
            Err(CompletionRefusal::IdentityExhausted)
        );
        assert!(!protocol.is_in_flight());
    }

    #[test]
    fn an_answer_that_is_not_text_is_no_suggestion() {
        assert_eq!(suggestion_from(&[0xff, 0xfe, 0xfd]), None);
    }

    #[test]
    fn a_long_answer_is_cut_on_a_character_boundary() {
        let long = "é".repeat(wire::MAX_COMPOSER_COMPLETION_BYTES);
        let kept = suggestion_from(long.as_bytes()).unwrap_or_default();
        assert!(kept.len() <= wire::MAX_COMPOSER_COMPLETION_BYTES);
        assert!(
            !kept.is_empty() && long.starts_with(&kept),
            "the cut is a prefix of what came back, never a re-encoding of it"
        );
    }
}
