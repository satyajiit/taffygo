// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One pasted or stored key, proved by one bounded completion (decision 0083).
//!
//! Probing is not routing. The probe never consults route selection, never
//! binds a task, and never enters the task ledger: it is one deliberately
//! tiny model call against the provider the person named, whose only product
//! is a verdict. The composition composes the call (cheapest catalog model,
//! a fixed one-word prompt, a sixteen-token answer allowance); this protocol
//! owns what is left — the single flight, the effect identity, the
//! classification of what came back, and the latest verdict per provider
//! that the status projection carries.
//!
//! The classification is deliberately three-valued in meaning even though the
//! vocabulary has eleven members: `Usable` and the definitive failures
//! (`Auth`, `Billing`, `ModelNotFound`) subtract, and every indefinite member
//! means the provider was not definitively heard — an unreachable endpoint is
//! not a wrong key, and a surface offers to save anyway rather than claiming
//! one. One member is filed without any call at all: `NoModelListed` is the
//! standing answer for a provider that has nothing to probe with, and it goes
//! through [`ProviderProbeProtocol::file_without_flight`] because the sheet
//! reads verdicts, and a refusal of the command was being read as a transient
//! fault in the asking.
//!
//! The effect identity is the other thing this module owns, and decision 0099
//! is why it looks the way it does. It names the provider, the browser session
//! and service incarnation the asking happened in, and which asking of that
//! incarnation it is. Every part is load bearing even though decision 0100
//! makes probes live rather than journalled: the browser still correlates a
//! pending callback by this string, so another incarnation may not repeat it.

use std::collections::BTreeMap;

use model_router::wire::ServerKind;
use task_engine::{BrowserSessionId, MAX_BROWSER_SESSION_ID_BYTES};

use crate::contract::ServiceGeneration;
use crate::provider::{CustomModel, ProviderId, MAX_PROVIDER_ID_BYTES};
use crate::wire;

/// The identity prefix of a probe that proves a key.
const KEY_PROBE_PREFIX: &str = "provider-probe";

/// The identity prefix of a probe that proves an address.
///
/// Two prefixes rather than one, because the two probes ask different
/// questions and a log that cannot tell them apart cannot say which one a
/// person was refused.
const ENDPOINT_PROBE_PREFIX: &str = "custom-endpoint-probe";

/// Decimal width of the widest service generation, which the browser counts
/// in a `u64`.
const MAX_GENERATION_DIGITS: usize = 20;

/// Decimal width of the widest attempt ordinal. See [`ProviderProbeProtocol`]
/// for why the counter is a `u32`.
const MAX_ATTEMPT_DIGITS: usize = 10;

/// The longest identity this module can mint.
///
/// Computed from the four contract bounds that feed it rather than measured,
/// so widening any of them fails the assertion below at compile time instead
/// of failing the browser's identifier check at run time — where an over-long
/// identity is refused with nothing to see, which is the same shape of silent
/// failure decision 0099 exists to stop repeating.
pub const MAX_PROBE_EFFECT_ID_BYTES: usize = ENDPOINT_PROBE_PREFIX.len()
    + 1
    + MAX_PROVIDER_ID_BYTES
    + 1
    + MAX_BROWSER_SESSION_ID_BYTES
    + 1
    + MAX_GENERATION_DIGITS
    + 1
    + MAX_ATTEMPT_DIGITS;

const _: () = {
    assert!(
        KEY_PROBE_PREFIX.len() <= ENDPOINT_PROBE_PREFIX.len(),
        "the bound above is computed from whichever prefix is the longer"
    );
    assert!(
        MAX_PROBE_EFFECT_ID_BYTES <= wire::MAX_IDENTIFIER_BYTES,
        "a probe identity the browser refuses for its length is a probe that \
         never happens and says nothing, which is the failure being fixed"
    );
};

/// The answer allowance the probe body asks for, in tokens.
pub const PROBE_ANSWER_TOKENS: u64 = 16;

/// The completion size cap the probe effect carries, in bytes. Generous for
/// sixteen tokens, and small enough that a misbehaving endpoint cannot make
/// the browser buffer a real answer on the probe's budget.
pub const PROBE_MAX_OUTPUT_BYTES: u32 = 4_096;

/// What one bounded probe call proved.
///
/// Held equal to the Core API's `ProviderProbeVerdictView` member for member;
/// the status contributor projects it one to one.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProbeVerdict {
    /// The provider answered the probe on this credential.
    Usable,
    /// Definitive: the provider refused the credential itself.
    Auth,
    /// Definitive: the account behind the credential cannot pay.
    Billing,
    /// Indefinite: the provider is rate limiting; the key was not judged.
    RateLimit,
    /// Indefinite: the provider is overloaded; the key was not judged.
    Overloaded,
    /// Indefinite: the call ran out of time before an answer.
    Timeout,
    /// Indefinite: the provider was not reached at all.
    Network,
    /// Definitive: the probe's model is unknown at this endpoint — a catalog
    /// defect rather than a key defect, and said as one.
    ModelNotFound,
    /// Indefinite: the answer fit no closed category.
    Unknown,
    /// An address a person typed answered, and what it said about itself
    /// travels beside this verdict. Not a statement about a key: an endpoint
    /// probe proves that something is there and what it offers, and the
    /// credential behind it is judged, if at all, by a key probe.
    EndpointReached,
    /// Definitive about the catalog and indefinite about the key: the provider
    /// carries no model this build could probe with, so nothing was sent and
    /// nothing was judged. A provider that serves its own list is in this
    /// state until a credential is saved and the list fetched (decision 0098
    /// section 1). Filed without a flight — see
    /// [`ProviderProbeProtocol::file_without_flight`] — because it is the
    /// standing answer to the question rather than a refusal of the asking.
    NoModelListed,
}

impl ProbeVerdict {
    /// Whether this verdict is a definitive answer about the key.
    ///
    /// Only a definitive answer subtracts; an indefinite one leaves the key
    /// unjudged and the surface offering to save anyway.
    #[must_use]
    pub fn is_definitive(self) -> bool {
        matches!(
            self,
            Self::Usable | Self::Auth | Self::Billing | Self::ModelNotFound | Self::EndpointReached
        )
    }
}

/// The transport facts one probe dispatch produced, as the browser reports
/// them: the coarse effect status, and the provider's own HTTP status when
/// the provider was reached (zero when it was not).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProbeOutcome {
    /// The effect's terminal status.
    pub status: wire::EffectStatus,
    /// The provider's HTTP status code; zero when never reached.
    pub provider_http_status: u32,
}

/// Classifies one probe outcome into the closed verdict.
///
/// The HTTP status is read before the coarsened effect status, because the
/// effect vocabulary folds different provider answers into one member (a 401,
/// a 402 and a 403 all arrive as a denial) and the verdict exists to keep
/// them apart. Order inside the HTTP match is by specificity, not by number.
#[must_use]
pub fn classify_probe_outcome(outcome: ProbeOutcome) -> ProbeVerdict {
    match outcome.provider_http_status {
        401 | 403 => return ProbeVerdict::Auth,
        402 => return ProbeVerdict::Billing,
        404 => return ProbeVerdict::ModelNotFound,
        429 => return ProbeVerdict::RateLimit,
        500 | 502 | 503 | 529 => return ProbeVerdict::Overloaded,
        _ => {}
    }
    match outcome.status {
        wire::EffectStatus::Completed => {
            if (200..300).contains(&outcome.provider_http_status) {
                ProbeVerdict::Usable
            } else {
                ProbeVerdict::Unknown
            }
        }
        wire::EffectStatus::DeadlineExceeded => ProbeVerdict::Timeout,
        wire::EffectStatus::Unavailable if outcome.provider_http_status == 0 => {
            ProbeVerdict::Network
        }
        _ => ProbeVerdict::Unknown,
    }
}

/// What one endpoint probe read from the server's own answer.
///
/// `model_count` and `models` are kept apart on purpose and all the way up:
/// the count is what the server named, the list is what survived the bound,
/// and reading the list's length as the count is the silent truncation
/// decisions 0096 section 5 and 0098 section 4 both refuse.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProbeEndpoint {
    /// The runtime the server identified itself as. An endpoint that named
    /// none is `OpenaiCompatible`, which is the contract's own word for it
    /// rather than a guess.
    pub server_kind: ServerKind,
    /// How many models it named, before the bound.
    pub model_count: u32,
    /// The models that survived the bound, in the order it listed them.
    pub models: Vec<CustomModel>,
    /// The base the OpenAI-shaped API was proved at, absent when nothing was.
    pub proved_base: Option<String>,
}

/// One delivered endpoint probe, as the browser's prober reports it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EndpointProbeOutcome {
    /// Whether anything answered at all.
    pub reached: bool,
    /// What it said about itself, present only when it answered.
    pub endpoint: Option<ProbeEndpoint>,
}

/// The latest verdict for one provider, as the status projection carries it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProbeDisplay {
    /// The provider the verdict is about.
    pub provider_id: String,
    /// What the one bounded call proved.
    pub verdict: ProbeVerdict,
    /// When the verdict was reached, browser monotonic milliseconds.
    pub at_monotonic_ms: u64,
    /// What a person's own endpoint said about itself, present only for an
    /// endpoint probe that reached one.
    pub endpoint: Option<ProbeEndpoint>,
}

/// Why a probe command was refused before any effect existed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProbeRefusal {
    /// One probe is already in flight; the next waits for its verdict. The
    /// single flight is what makes the effect identity unambiguous, and a
    /// person testing a key is looking at one sheet.
    ProbeInFlight,
    /// The incarnation has minted every representable attempt ordinal.
    /// Refusing is safer than repeating the final live identity forever.
    IdentityExhausted,
}

/// Profile-scoped probe state: the single flight and the latest verdict per
/// provider. No socket, no clock, no key material — the credential crosses
/// this type as nothing at all, because the effect the composition builds is
/// the only carrier the handle has.
///
/// It also carries the incarnation it mints identities in, and it has no
/// `Default` on purpose. A probe protocol that could be built without one is
/// exactly the protocol whose identities repeated on every restart, so the
/// incarnation is a constructor argument rather than something a caller may
/// forget.
///
/// The attempt ordinal is a `u32` so the rendered identity has a width the
/// bound above can be computed from. Saturating it would repeat an identity,
/// and that is worth saying plainly: it takes four billion probes inside one
/// service incarnation, each of which the single flight holds open for a
/// network round trip to a model provider, so the counter cannot be walked
/// there by anything a person or this product does.
#[derive(Clone, Debug)]
pub struct ProviderProbeProtocol {
    in_flight: Option<InFlightProbe>,
    verdicts: BTreeMap<String, ProbeRecord>,
    /// The browser session and service incarnation this protocol belongs to,
    /// rendered once because neither changes while it lives.
    incarnation: String,
    attempts: u32,
}

#[derive(Clone, Debug)]
struct InFlightProbe {
    effect_id: String,
    provider_id: String,
}

#[derive(Clone, Debug)]
struct ProbeRecord {
    verdict: ProbeVerdict,
    at_monotonic_ms: u64,
    endpoint: Option<ProbeEndpoint>,
}

impl ProviderProbeProtocol {
    /// Binds one probe protocol to the incarnation whose identities it mints.
    ///
    /// Both arguments arrive as their validated types rather than as strings,
    /// which is what makes [`MAX_PROBE_EFFECT_ID_BYTES`] a proof rather than a
    /// hope: nothing can reach `claim` carrying a session identity or a
    /// provider identity longer than its own contract bound.
    #[must_use]
    pub fn new(browser_session_id: &BrowserSessionId, generation: ServiceGeneration) -> Self {
        Self {
            in_flight: None,
            verdicts: BTreeMap::new(),
            incarnation: format!("{}-{}", browser_session_id.as_str(), generation.value()),
            attempts: 0,
        }
    }

    /// Claims the single flight and mints the effect identity.
    ///
    /// The identity names the provider being probed, the incarnation doing the
    /// asking, and which asking of that incarnation this is — what the effect
    /// is about and in what, never a position in any batch (the delivery-plane
    /// collision is why that rule exists).
    pub fn begin(&mut self, provider_id: &ProviderId) -> Result<String, ProbeRefusal> {
        self.claim(KEY_PROBE_PREFIX, provider_id)
    }

    /// Claims the same single flight for an endpoint probe.
    ///
    /// One flight covers both kinds deliberately. A person testing an address
    /// and a person testing a key are the same person on the same sheet, and
    /// two flights would let an endpoint verdict and a key verdict for one
    /// provider land in either order. They share the ordinal for the same
    /// reason they share the flight.
    pub fn begin_endpoint(&mut self, provider_id: &ProviderId) -> Result<String, ProbeRefusal> {
        self.claim(ENDPOINT_PROBE_PREFIX, provider_id)
    }

    /// Mints one identity that no other incarnation of this profile can mint.
    ///
    /// The ordinal alone cannot do this and never could: it counts inside one
    /// process, while a late browser adapter callback can outlive that core
    /// process. The incarnation supplies the two facts the ordinal is missing
    /// — which browser session asked, which service generation within it —
    /// and between them no two askings anywhere agree on all three.
    fn claim(&mut self, prefix: &str, provider_id: &ProviderId) -> Result<String, ProbeRefusal> {
        if self.in_flight.is_some() {
            return Err(ProbeRefusal::ProbeInFlight);
        }
        self.attempts = self
            .attempts
            .checked_add(1)
            .ok_or(ProbeRefusal::IdentityExhausted)?;
        let attempt = self.attempts;
        let effect_id = format!(
            "{prefix}-{provider}-{incarnation}-{attempt}",
            provider = provider_id.as_str(),
            incarnation = self.incarnation,
        );
        self.in_flight = Some(InFlightProbe {
            effect_id: effect_id.clone(),
            provider_id: provider_id.as_str().to_owned(),
        });
        Ok(effect_id)
    }

    /// Releases the flight without a verdict, for a submit that failed after
    /// claiming it (the effect was never handed to the browser).
    pub fn abandon(&mut self, effect_id: &str) {
        if self
            .in_flight
            .as_ref()
            .is_some_and(|flight| flight.effect_id == effect_id)
        {
            self.in_flight = None;
        }
    }

    /// Judges one delivered probe result on the ordered core sequence.
    ///
    /// Returns the provider the verdict was filed under, or `None` when no
    /// probe with this effect identity is in flight — a late or foreign
    /// result answers nothing that was asked and records nothing.
    pub fn deliver(
        &mut self,
        effect_id: &str,
        outcome: ProbeOutcome,
        now_monotonic_ms: u64,
    ) -> Option<(String, ProbeVerdict)> {
        let flight = self
            .in_flight
            .take_if(|flight| flight.effect_id == effect_id)?;
        let verdict = classify_probe_outcome(outcome);
        self.verdicts.insert(
            flight.provider_id.clone(),
            ProbeRecord {
                verdict,
                at_monotonic_ms: now_monotonic_ms,
                // A key probe learns nothing about what kind of endpoint
                // answered, so a verdict it files never carries a shape - and
                // it clears one an earlier endpoint probe left, because the
                // row now describes a different question.
                endpoint: None,
            },
        );
        Some((flight.provider_id, verdict))
    }

    /// Judges one delivered endpoint probe on the ordered core sequence.
    ///
    /// Returns the provider the verdict was filed under, or `None` when no
    /// probe with this effect identity is in flight. A reached endpoint files
    /// `EndpointReached` and the shape it reported; an unreached one files
    /// `Network` and no shape, because there is nothing to describe.
    pub fn deliver_endpoint(
        &mut self,
        effect_id: &str,
        outcome: EndpointProbeOutcome,
        now_monotonic_ms: u64,
    ) -> Option<String> {
        let flight = self
            .in_flight
            .take_if(|flight| flight.effect_id == effect_id)?;
        let reached = outcome.reached && outcome.endpoint.is_some();
        self.verdicts.insert(
            flight.provider_id.clone(),
            ProbeRecord {
                verdict: if reached {
                    ProbeVerdict::EndpointReached
                } else {
                    ProbeVerdict::Network
                },
                at_monotonic_ms: now_monotonic_ms,
                endpoint: reached.then_some(outcome.endpoint).flatten(),
            },
        );
        Some(flight.provider_id)
    }

    /// Files a verdict for a probe that never flew.
    ///
    /// The composition calls this when there is no call to make — a provider
    /// with no listed model — and the answer is a fact about the catalog that
    /// stands until the catalog changes. Reporting it as a command refusal was
    /// the defect: a refusal reads as transient ("could not run right now"),
    /// and a person retries a state no retry can change. Filing it as a
    /// verdict puts it where the sheet already looks.
    ///
    /// Nothing else moves. The flight is not claimed, so a probe already in
    /// flight for another provider is undisturbed and a later `begin` is not
    /// blocked; no identity is minted, so the attempt ordinal is untouched and
    /// nothing reaches the browser's correlation table. The record carries no
    /// endpoint shape, because no endpoint answered.
    pub fn file_without_flight(
        &mut self,
        provider_id: &ProviderId,
        verdict: ProbeVerdict,
        now_monotonic_ms: u64,
    ) {
        self.verdicts.insert(
            provider_id.as_str().to_owned(),
            ProbeRecord {
                verdict,
                at_monotonic_ms: now_monotonic_ms,
                endpoint: None,
            },
        );
    }

    /// Whether a probe is currently in flight.
    #[must_use]
    pub fn in_flight(&self) -> bool {
        self.in_flight.is_some()
    }

    /// The latest verdict per provider, in stable provider order, for the
    /// status contributor.
    #[must_use]
    pub fn display(&self) -> Vec<ProbeDisplay> {
        self.verdicts
            .iter()
            .map(|(provider_id, record)| ProbeDisplay {
                provider_id: provider_id.clone(),
                verdict: record.verdict,
                at_monotonic_ms: record.at_monotonic_ms,
                endpoint: record.endpoint.clone(),
            })
            .collect()
    }
}

#[cfg(test)]
mod tests;
