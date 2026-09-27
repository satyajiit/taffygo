// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use task_engine::{BrowserSessionId, MAX_BROWSER_SESSION_ID_BYTES};

use super::{
    classify_probe_outcome, EndpointProbeOutcome, ProbeOutcome, ProbeRefusal, ProbeVerdict,
    ProviderProbeProtocol, MAX_PROBE_EFFECT_ID_BYTES,
};
use crate::contract::ServiceGeneration;
use crate::provider::{ProviderId, MAX_PROVIDER_ID_BYTES};
use crate::wire;

fn outcome(status: wire::EffectStatus, http: u32) -> ProbeOutcome {
    ProbeOutcome {
        status,
        provider_http_status: http,
    }
}

fn provider(value: &str) -> ProviderId {
    match ProviderId::new(value) {
        Ok(identity) => identity,
        Err(error) => unreachable!("the test names a valid provider: {error:?}"),
    }
}

fn session(value: &str) -> BrowserSessionId {
    match BrowserSessionId::new(value) {
        Ok(identity) => identity,
        Err(error) => unreachable!("the test names a valid browser session: {error:?}"),
    }
}

/// One protocol in the first incarnation of one browser session.
fn protocol() -> ProviderProbeProtocol {
    ProviderProbeProtocol::new(&session("browser-session-1"), ServiceGeneration::INITIAL)
}

fn begin(protocol: &mut ProviderProbeProtocol, provider_id: &str) -> String {
    match protocol.begin(&provider(provider_id)) {
        Ok(effect_id) => effect_id,
        Err(refusal) => unreachable!("the flight was free: {refusal:?}"),
    }
}

#[test]
fn an_exhausted_attempt_counter_refuses_instead_of_repeating_an_identity() {
    let mut protocol = protocol();
    protocol.attempts = u32::MAX;
    assert_eq!(
        protocol.begin(&provider("anthropic")),
        Err(ProbeRefusal::IdentityExhausted)
    );
    assert!(!protocol.in_flight());
}

#[test]
fn the_http_status_is_read_before_the_coarsened_effect_status() {
    // All three arrive as one denial on the effect vocabulary; the verdict
    // keeps them apart, which is the whole reason the HTTP fact crosses.
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::Denied, 401)),
        ProbeVerdict::Auth
    );
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::Denied, 403)),
        ProbeVerdict::Auth
    );
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::Denied, 402)),
        ProbeVerdict::Billing
    );
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::Unavailable, 404)),
        ProbeVerdict::ModelNotFound
    );
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::Unavailable, 429)),
        ProbeVerdict::RateLimit
    );
    for overloaded in [500, 502, 503, 529] {
        assert_eq!(
            classify_probe_outcome(outcome(wire::EffectStatus::Unavailable, overloaded)),
            ProbeVerdict::Overloaded
        );
    }
}

#[test]
fn a_completed_two_hundred_is_usable_and_nothing_else_is() {
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::Completed, 200)),
        ProbeVerdict::Usable
    );
    // A completion the browser accepted over a status outside 2xx fits no
    // closed category and must not read as a working key.
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::Completed, 300)),
        ProbeVerdict::Unknown
    );
}

#[test]
fn unreached_and_out_of_time_are_indefinite_answers() {
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::Unavailable, 0)),
        ProbeVerdict::Network
    );
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::DeadlineExceeded, 0)),
        ProbeVerdict::Timeout
    );
    assert_eq!(
        classify_probe_outcome(outcome(wire::EffectStatus::OutcomeUnknown, 0)),
        ProbeVerdict::Unknown
    );
}

#[test]
fn only_the_definitive_verdicts_subtract() {
    for verdict in [
        ProbeVerdict::Usable,
        ProbeVerdict::Auth,
        ProbeVerdict::Billing,
        ProbeVerdict::ModelNotFound,
        ProbeVerdict::EndpointReached,
    ] {
        assert!(verdict.is_definitive());
    }
    for verdict in [
        ProbeVerdict::RateLimit,
        ProbeVerdict::Overloaded,
        ProbeVerdict::Timeout,
        ProbeVerdict::Network,
        ProbeVerdict::Unknown,
        // Definitive about the catalog, and exactly not about the key: a
        // surface offers to save, which is the indefinite side.
        ProbeVerdict::NoModelListed,
    ] {
        assert!(!verdict.is_definitive());
    }
}

#[test]
fn a_verdict_filed_without_a_flight_shows_and_claims_nothing() {
    let mut protocol = protocol();
    protocol.file_without_flight(&provider("openrouter"), ProbeVerdict::NoModelListed, 5_000);

    // It is on the sheet, with no endpoint shape, and it is not a judgement
    // of the key.
    let display = protocol.display();
    assert_eq!(display.len(), 1);
    let Some(row) = display.first() else {
        unreachable!("one verdict was just filed");
    };
    assert_eq!(row.provider_id, "openrouter");
    assert_eq!(row.verdict, ProbeVerdict::NoModelListed);
    assert_eq!(row.at_monotonic_ms, 5_000);
    assert!(row.endpoint.is_none());
    assert!(!row.verdict.is_definitive());

    // Nothing flew: the flight is free, and the first minted identity is
    // still the first, because filing consumed no attempt ordinal.
    assert!(!protocol.in_flight());
    let effect_id = begin(&mut protocol, "anthropic");
    assert_eq!(effect_id, "provider-probe-anthropic-browser-session-1-1-1");
}

#[test]
fn filing_without_a_flight_leaves_a_flight_in_progress_alone() {
    let mut protocol = protocol();
    let effect_id = begin(&mut protocol, "anthropic");
    protocol.file_without_flight(&provider("openrouter"), ProbeVerdict::NoModelListed, 5_000);
    assert!(protocol.in_flight());

    // The in-flight probe still delivers under its own identity, and both
    // rows stand side by side.
    assert_eq!(
        protocol.deliver(
            &effect_id,
            outcome(wire::EffectStatus::Completed, 200),
            6_000
        ),
        Some(("anthropic".to_owned(), ProbeVerdict::Usable))
    );
    assert_eq!(protocol.display().len(), 2);
}

#[test]
fn one_probe_flies_at_a_time_and_the_identity_names_the_provider() {
    let mut protocol = protocol();
    let effect_id = begin(&mut protocol, "anthropic");
    assert_eq!(effect_id, "provider-probe-anthropic-browser-session-1-1-1");
    assert!(protocol.in_flight());
    assert_eq!(
        protocol.begin(&provider("xai")),
        Err(ProbeRefusal::ProbeInFlight)
    );
    // The single flight covers both kinds, so an address cannot be asked
    // about while a key is being judged (decision 0083).
    assert_eq!(
        protocol.begin_endpoint(&provider("my-gateway")),
        Err(ProbeRefusal::ProbeInFlight)
    );

    let filed = protocol.deliver(
        &effect_id,
        outcome(wire::EffectStatus::Completed, 200),
        9_000,
    );
    assert_eq!(filed, Some(("anthropic".to_owned(), ProbeVerdict::Usable)));
    assert!(!protocol.in_flight());

    let display = protocol.display();
    assert_eq!(display.len(), 1);
    let Some(row) = display.first() else {
        unreachable!("one verdict was just filed");
    };
    assert_eq!(row.provider_id, "anthropic");
    assert_eq!(row.verdict, ProbeVerdict::Usable);
    assert_eq!(row.at_monotonic_ms, 9_000);
}

#[test]
fn a_foreign_or_late_result_records_nothing() {
    let mut protocol = protocol();
    assert_eq!(
        protocol.deliver(
            "provider-probe-anthropic-browser-session-1-1-1",
            outcome(wire::EffectStatus::Completed, 200),
            1_000,
        ),
        None
    );

    let effect_id = begin(&mut protocol, "anthropic");
    // A result naming another identity leaves the flight standing.
    assert_eq!(
        protocol.deliver(
            "provider-probe-anthropic-browser-session-1-1-999",
            outcome(wire::EffectStatus::Completed, 200),
            1_000,
        ),
        None
    );
    assert!(protocol.in_flight());
    assert!(protocol
        .deliver(
            &effect_id,
            outcome(wire::EffectStatus::Completed, 200),
            1_000
        )
        .is_some());
}

#[test]
fn a_second_probe_replaces_the_providers_verdict() {
    let mut protocol = protocol();
    let first = begin(&mut protocol, "anthropic");
    protocol.deliver(&first, outcome(wire::EffectStatus::Denied, 401), 1_000);
    let second = begin(&mut protocol, "anthropic");
    assert_eq!(second, "provider-probe-anthropic-browser-session-1-1-2");
    protocol.deliver(&second, outcome(wire::EffectStatus::Completed, 200), 2_000);

    let display = protocol.display();
    assert_eq!(display.len(), 1);
    let Some(row) = display.first() else {
        unreachable!("one verdict is held");
    };
    assert_eq!(row.verdict, ProbeVerdict::Usable);
    assert_eq!(row.at_monotonic_ms, 2_000);
}

#[test]
fn an_abandoned_flight_frees_the_slot_without_a_verdict() {
    let mut protocol = protocol();
    let effect_id = begin(&mut protocol, "anthropic");
    // Abandonment matches on the identity that was minted, so it has to be
    // given exactly that identity and nothing near it.
    protocol.abandon("provider-probe-anthropic-browser-session-1-1-1");
    assert!(!protocol.in_flight());
    assert!(protocol.display().is_empty());
    assert!(protocol.begin(&provider("anthropic")).is_ok());
    assert_eq!(effect_id, "provider-probe-anthropic-browser-session-1-1-1");
}

// --- the identity is new in every incarnation (decision 0099) --------------

/// The property the browser's intent journal actually needs.
///
/// `core_effect_journal` is keyed on `effect_id` alone — the generation is a
/// stored column, not part of the key — and `CommitIntent` refuses an
/// identity it already holds, because an existing row is a durable execution
/// claim. The journal outlives the utility process and the browser, so an
/// identity that any later incarnation can mint again is a probe that is
/// refused before it is ever dispatched, for the life of the profile.
///
/// This asks for distinctness across the two axes that reset, rather than for
/// a spelling: the ordinal restarts with every service incarnation, and the
/// generation restarts at one with every browser launch. The old identity
/// carried only the ordinal, so all six askings below minted
/// `provider-probe-anthropic-1` and five of them were dead on arrival.
#[test]
fn no_two_incarnations_mint_the_same_identity_for_one_provider() {
    let mut minted: Vec<String> = Vec::new();
    for browser_session in ["browser-session-a", "browser-session-b"] {
        for generation in [1_u64, 2, 3] {
            // A fresh protocol per incarnation is the point: this is what the
            // composition root builds on every service launch, and its
            // ordinal starts again from nothing every time.
            let mut protocol = ProviderProbeProtocol::new(
                &session(browser_session),
                ServiceGeneration::new(generation),
            );
            let effect_id = begin(&mut protocol, "anthropic");
            assert!(
                !minted.contains(&effect_id),
                "{browser_session} generation {generation} minted an identity a \
                 previous incarnation already claimed: {effect_id}"
            );
            minted.push(effect_id);
        }
    }
    assert_eq!(minted.len(), 6);
}

/// The endpoint probe shares the counter, so it shares the defect and the fix.
///
/// It is not journalled today — the browser answers it where it arrives — but
/// that is the browser's choice about one effect kind, not a property of this
/// identity, and the two probes are minted by one function precisely so they
/// cannot drift apart.
#[test]
fn the_endpoint_probe_identity_is_new_in_every_incarnation_too() {
    let mut minted: Vec<String> = Vec::new();
    for generation in [1_u64, 2] {
        let mut protocol = ProviderProbeProtocol::new(
            &session("browser-session-a"),
            ServiceGeneration::new(generation),
        );
        let effect_id = match protocol.begin_endpoint(&provider("my-gateway")) {
            Ok(effect_id) => effect_id,
            Err(refusal) => unreachable!("the flight was free: {refusal:?}"),
        };
        assert!(effect_id.starts_with("custom-endpoint-probe-my-gateway-"));
        assert!(
            !minted.contains(&effect_id),
            "identity repeated: {effect_id}"
        );

        // Delivery matches the identity that was minted, whatever it spells.
        assert_eq!(
            protocol.deliver_endpoint(
                &effect_id,
                EndpointProbeOutcome {
                    reached: false,
                    endpoint: None,
                },
                7_000,
            ),
            Some("my-gateway".to_owned())
        );
        minted.push(effect_id);
    }
}

/// The identity must stay inside the browser's identifier bound.
///
/// An over-long identity is refused by `IsValidCoreEffectEnvelope` with
/// nothing to see, which would swap one silent failure for another. The
/// compile-time assertion in the module proves the arithmetic — it is not
/// repeated here, because a constant compared to a constant inside a test is
/// a thing clippy refuses and is right to; this proves the other half, that
/// what `claim` actually renders fits inside what that arithmetic promised.
#[test]
fn the_longest_identity_this_module_can_mint_fits_the_identifier_bound() {
    let widest_provider = "p".repeat(MAX_PROVIDER_ID_BYTES);
    let widest_session = "s".repeat(MAX_BROWSER_SESSION_ID_BYTES);
    let mut protocol =
        ProviderProbeProtocol::new(&session(&widest_session), ServiceGeneration::new(u64::MAX));
    let effect_id = match protocol.begin_endpoint(&provider(&widest_provider)) {
        Ok(effect_id) => effect_id,
        Err(refusal) => unreachable!("the flight was free: {refusal:?}"),
    };
    // Everything but the attempt ordinal is at its bound here; the ordinal's
    // own width is what the module's assertion covers.
    assert!(effect_id.len() <= MAX_PROBE_EFFECT_ID_BYTES);
    assert!(effect_id.len() <= wire::MAX_IDENTIFIER_BYTES);
}
