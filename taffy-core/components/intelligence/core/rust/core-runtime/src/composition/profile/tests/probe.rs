// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Proving a pasted key with one bounded, content-free call.

use core_service_types as wire;

use super::{built_runtime, probe_command, save_provider_credential_command};
use crate::composition::profile::ProfileServiceRuntime;

#[test]
fn a_probe_composes_one_bounded_content_free_call_and_publishes_a_verdict() {
    use crate::composition::profile::probe::ProbeCommandError;
    use crate::probe::ProbeOutcome;

    let mut runtime = built_runtime();
    let effect = runtime
        .submit_probe_command(&probe_command("anthropic", "transient-handle-1"), 1_000)
        .unwrap_or_else(|_| unreachable!())
        .unwrap_or_else(|| unreachable!("a listed model composes a flight"));
    assert_eq!(effect.kind, wire::EffectKind::ModelRequest);
    assert_eq!(effect.retry_class, wire::RetryClass::Never);
    assert_eq!(
        effect.effect_id,
        "provider-probe-anthropic-browser-session-1-1-1"
    );
    let request = effect
        .model_request
        .as_ref()
        .unwrap_or_else(|| unreachable!("a probe effect carries a model request"));
    assert!(request.probe);
    assert!(request.task_id.is_empty(), "no task owns a probe");
    assert_eq!(request.provider_id, "anthropic");
    // The catalog's cheapest enabled anthropic model, deterministically.
    assert_eq!(request.model_id, "claude-haiku-4-5");
    assert_eq!(request.disclosure, wire::DisclosureClass::ContentFree);
    assert_eq!(
        request.credential_handle.as_deref(),
        Some("transient-handle-1")
    );
    assert!(!request.request_body.is_empty());
    assert!(request.static_headers.is_empty());

    // One probe at a time: the flight refuses a second sheet's ask.
    assert_eq!(
        runtime.submit_probe_command(&probe_command("xai", "handle-2"), 1_000),
        Err(ProbeCommandError::ProbeInFlight)
    );

    // The delivered terminal files the verdict and asks for publication.
    assert!(runtime.deliver_probe_result(
        "provider-probe-anthropic-browser-session-1-1-1",
        ProbeOutcome {
            status: wire::EffectStatus::Denied,
            provider_http_status: 401,
        },
        9_000,
    ));
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(status.provider_probes.len(), 1);
    let row = status
        .provider_probes
        .first()
        .unwrap_or_else(|| unreachable!());
    assert_eq!(row.provider_id, "anthropic");
    assert_eq!(row.verdict, core_api_types::ProviderProbeVerdictView::Auth);
    assert_eq!(row.at_monotonic_ms, 9_000);
}

#[test]
fn a_vendor_refusal_reaches_the_roster_and_a_spend_takes_it_down_again() {
    use crate::probe::ProbeOutcome;

    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_provider_credential_command("anthropic", "handle-1"))
        .unwrap_or_else(|_| unreachable!());
    let effect = runtime
        .submit_probe_command(&probe_command("anthropic", "handle-1"), 1_000)
        .unwrap_or_else(|_| unreachable!())
        .unwrap_or_else(|| unreachable!("a listed model composes a flight"));
    assert!(runtime.deliver_probe_result(
        &effect.effect_id,
        ProbeOutcome {
            status: wire::EffectStatus::Denied,
            provider_http_status: 429,
        },
        9_000,
    ));

    // A rate limit is not a statement about the key, and the credential
    // state cannot carry it: every one of its members is about the key.
    let row = roster_row(&runtime, "anthropic");
    assert_eq!(
        row.last_refusal,
        Some(core_api_types::ProviderRefusalStateView {
            refusal: core_api_types::ProviderRefusalView::RateLimit,
            at_monotonic_ms: 9_000,
        })
    );
    assert_eq!(
        row.stored.as_ref().map(|stored| stored.state),
        Some(core_api_types::ProviderCredentialStateView::Usable),
        "the key still works; that is exactly why a second fact was needed"
    );

    // The vendor spent on the next call, so the banner comes down rather
    // than standing for the life of the process.
    let second = runtime
        .submit_probe_command(&probe_command("anthropic", "handle-1"), 1_000)
        .unwrap_or_else(|_| unreachable!())
        .unwrap_or_else(|| unreachable!("a listed model composes a flight"));
    assert!(runtime.deliver_probe_result(
        &second.effect_id,
        ProbeOutcome {
            status: wire::EffectStatus::Completed,
            provider_http_status: 200,
        },
        21_000,
    ));
    assert_eq!(roster_row(&runtime, "anthropic").last_refusal, None);
}

#[test]
fn a_wrong_key_leaves_a_standing_refusal_alone() {
    use crate::probe::ProbeOutcome;

    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_provider_credential_command("anthropic", "handle-1"))
        .unwrap_or_else(|_| unreachable!());
    let effect = runtime
        .submit_probe_command(&probe_command("anthropic", "handle-1"), 1_000)
        .unwrap_or_else(|_| unreachable!())
        .unwrap_or_else(|| unreachable!("a listed model composes a flight"));
    assert!(runtime.deliver_probe_result(
        &effect.effect_id,
        ProbeOutcome {
            status: wire::EffectStatus::Denied,
            provider_http_status: 402,
        },
        9_000,
    ));
    let second = runtime
        .submit_probe_command(&probe_command("anthropic", "handle-1"), 1_000)
        .unwrap_or_else(|_| unreachable!())
        .unwrap_or_else(|| unreachable!("a listed model composes a flight"));
    assert!(runtime.deliver_probe_result(
        &second.effect_id,
        ProbeOutcome {
            status: wire::EffectStatus::Denied,
            provider_http_status: 401,
        },
        21_000,
    ));
    assert_eq!(
        roster_row(&runtime, "anthropic").last_refusal,
        Some(core_api_types::ProviderRefusalStateView {
            refusal: core_api_types::ProviderRefusalView::Billing,
            at_monotonic_ms: 9_000,
        }),
        "a rejected key says nothing about whether the account can pay, so \
         clearing the billing fact on it would hide a true one"
    );
}

fn roster_row(
    runtime: &ProfileServiceRuntime,
    provider_id: &str,
) -> core_api_types::ProviderRosterEntry {
    runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!())
        .provider_roster
        .into_iter()
        .find(|entry| entry.provider_id == provider_id)
        .unwrap_or_else(|| unreachable!("the baseline ships this provider"))
}

#[test]
fn a_probe_refuses_before_any_flight_when_it_cannot_be_composed() {
    use crate::composition::profile::probe::ProbeCommandError;

    let mut runtime = built_runtime();
    assert_eq!(
        runtime.submit_probe_command(&probe_command("no-such-provider", "handle-1"), 1_000),
        Err(ProbeCommandError::UnknownProvider)
    );
    // A provider that serves its own list has nothing to probe with before
    // any listing. That is a fact about the catalog and not about the
    // asking, so the command is accepted with no flight and the answer is
    // filed straight onto the sheet — where it used to be a refusal the
    // bridge said as "could not run right now", about a state no retry
    // changes.
    assert_eq!(
        runtime.submit_probe_command(&probe_command("openrouter", "handle-1"), 2_000),
        Ok(None)
    );
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(status.provider_probes.len(), 1);
    let row = status
        .provider_probes
        .first()
        .unwrap_or_else(|| unreachable!());
    assert_eq!(row.provider_id, "openrouter");
    assert_eq!(
        row.verdict,
        core_api_types::ProviderProbeVerdictView::NoModelListed
    );
    assert_eq!(row.at_monotonic_ms, 2_000);
    assert!(row.endpoint.is_none());
    // Neither answer claimed the flight or spent an identity: the first
    // real probe is still the first.
    let effect = runtime
        .submit_probe_command(&probe_command("anthropic", "handle-1"), 3_000)
        .unwrap_or_else(|_| unreachable!())
        .unwrap_or_else(|| unreachable!("a listed model composes a flight"));
    assert_eq!(
        effect.effect_id,
        "provider-probe-anthropic-browser-session-1-1-1"
    );
}

#[test]
fn a_late_or_foreign_probe_result_files_and_publishes_nothing() {
    use crate::probe::ProbeOutcome;

    let mut runtime = built_runtime();
    assert!(!runtime.deliver_probe_result(
        "provider-probe-anthropic-browser-session-1-1-1",
        ProbeOutcome {
            status: wire::EffectStatus::Completed,
            provider_http_status: 200,
        },
        1_000,
    ));
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert!(status.provider_probes.is_empty());
}
