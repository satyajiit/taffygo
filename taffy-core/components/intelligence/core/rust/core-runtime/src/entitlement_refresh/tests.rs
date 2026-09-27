// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The entitlement refresh lifecycle, driven through every gate and verdict.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use super::{
    EntitlementFetchVerdict, EntitlementRefreshProtocol, ENTITLEMENT_REFRESH_DUE_MS,
    ENTITLEMENT_RETRY_FLOOR_MS, MAX_ENTITLEMENT_RESPONSE_BYTES,
};
use crate::wire;

const NOW: u64 = 1_800_000_000_000;
const GENERATION: u64 = 7;

fn summary(definitive_absent: bool) -> wire::EntitlementSummaryResult {
    wire::EntitlementSummaryResult {
        definitive_absent,
        plan_id: "plan-standard".to_owned(),
        model_ids: vec!["stub-primary".to_owned()],
        window_seconds: 3_600,
        requests_remaining: 9,
        credits_granted: 1_000,
        credits_remaining: 964,
        credit_unit_micros: 500,
        next_renewal_epoch_seconds: 1_788_912_000,
        valid_until_epoch_seconds: 0,
        minted_at_utc_ms: NOW,
        worker_host: "gateway.taffygo.invalid".to_owned(),
        gateway_host: "gateway.taffygo.invalid".to_owned(),
    }
}

fn plan(
    protocol: &mut EntitlementRefreshProtocol,
    reason: wire::EntitlementFetchReason,
    now: u64,
) -> Option<wire::EffectEnvelope> {
    protocol.begin_refresh(GENERATION, reason, now)
}

#[test]
fn a_bootstrap_poke_plans_one_network_fetch_effect() {
    let mut protocol = EntitlementRefreshProtocol::default();
    let envelope = plan(&mut protocol, wire::EntitlementFetchReason::Bootstrap, NOW)
        .expect("a first bootstrap poke plans");
    assert_eq!(envelope.kind, wire::EffectKind::NetworkRequest);
    assert_eq!(envelope.retry_class, wire::RetryClass::Idempotent);
    assert_eq!(envelope.effect_id, "entitlement-fetch-7-1");
    assert_eq!(envelope.operation.operation_id, "entitlement-refresh-7-1");
    let network = envelope.network_request.expect("a network body");
    assert_eq!(
        network.operation_kind,
        wire::AccountNetworkOperation::FetchEntitlement
    );
    assert_eq!(network.max_response_bytes, MAX_ENTITLEMENT_RESPONSE_BYTES);
    assert!(network.has_valid_body());
    assert_eq!(
        network.fetch_entitlement.expect("a fetch body").reason,
        wire::EntitlementFetchReason::Bootstrap
    );
}

#[test]
fn one_fetch_is_in_flight_at_a_time() {
    let mut protocol = EntitlementRefreshProtocol::default();
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Bootstrap, NOW).is_some());
    // Every reason is refused while one is out, the immediate ones included.
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::SignIn, NOW).is_none());
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Cadence, NOW).is_none());
    assert!(plan(
        &mut protocol,
        wire::EntitlementFetchReason::QuotaRefused,
        NOW
    )
    .is_none());
}

#[test]
fn cadence_replans_only_after_the_due_interval() {
    let mut protocol = EntitlementRefreshProtocol::default();
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Bootstrap, NOW).is_some());
    assert_eq!(
        protocol.deliver_fetch_result(Some(&summary(false)), NOW),
        EntitlementFetchVerdict::Installed {
            definitive_absent: false
        }
    );
    let fresh = NOW + ENTITLEMENT_REFRESH_DUE_MS - 1;
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Cadence, fresh).is_none());
    let due = NOW + ENTITLEMENT_REFRESH_DUE_MS;
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Cadence, due).is_some());
}

#[test]
fn a_quota_refusal_replans_immediately_but_not_in_a_tight_circle() {
    let mut protocol = EntitlementRefreshProtocol::default();
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Bootstrap, NOW).is_some());
    assert!(protocol
        .deliver_fetch_result(Some(&summary(false)), NOW)
        .eq(&EntitlementFetchVerdict::Installed {
            definitive_absent: false
        }));
    // A fresh summary does not gate the quota reason the way cadence is
    // gated: a refused dispatch is evidence the held summary is wrong.
    let soon = NOW + ENTITLEMENT_RETRY_FLOOR_MS;
    assert!(plan(
        &mut protocol,
        wire::EntitlementFetchReason::QuotaRefused,
        soon
    )
    .is_some());
    assert!(protocol
        .deliver_fetch_result(Some(&summary(false)), soon)
        .eq(&EntitlementFetchVerdict::Installed {
            definitive_absent: false
        }));
    // But two refusals a second apart plan one fetch, not two.
    let tight = soon + ENTITLEMENT_RETRY_FLOOR_MS - 1;
    assert!(plan(
        &mut protocol,
        wire::EntitlementFetchReason::QuotaRefused,
        tight
    )
    .is_none());
    let spaced = soon + ENTITLEMENT_RETRY_FLOOR_MS;
    assert!(plan(
        &mut protocol,
        wire::EntitlementFetchReason::QuotaRefused,
        spaced
    )
    .is_some());
}

#[test]
fn a_transport_failure_keeps_the_last_summary() {
    let mut protocol = EntitlementRefreshProtocol::default();
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Bootstrap, NOW).is_some());
    protocol.deliver_fetch_result(Some(&summary(false)), NOW);
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::SignIn, NOW + 1).is_some());
    assert_eq!(
        protocol.deliver_fetch_result(None, NOW + 1),
        EntitlementFetchVerdict::TransportFailed
    );
    // "Unreachable" is not "absent": the held summary still answers.
    assert_eq!(
        protocol.summary().map(|held| held.plan_id.as_str()),
        Some("plan-standard")
    );
    assert!(protocol.display().is_some());
}

#[test]
fn a_definitive_absence_installs_and_projects_nothing() {
    let mut protocol = EntitlementRefreshProtocol::default();
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Bootstrap, NOW).is_some());
    assert_eq!(
        protocol.deliver_fetch_result(Some(&summary(true)), NOW),
        EntitlementFetchVerdict::Installed {
            definitive_absent: true
        }
    );
    // The summary is held — it is the worker's answer — but no surface row
    // is projected from "you have no plan".
    assert!(protocol.summary().is_some());
    assert!(protocol.display().is_none());
}

#[test]
fn the_display_carries_exactly_the_five_surface_facts() {
    let mut protocol = EntitlementRefreshProtocol::default();
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Bootstrap, NOW).is_some());
    protocol.deliver_fetch_result(Some(&summary(false)), NOW);
    let display = protocol.display().expect("an entitled summary projects");
    assert_eq!(display.plan_id, "plan-standard");
    assert_eq!(display.credits_granted, 1_000);
    assert_eq!(display.credits_remaining, 964);
    assert_eq!(display.next_renewal_epoch_seconds, 1_788_912_000);
    assert_eq!(display.valid_until_epoch_seconds, 0);
}

#[test]
fn an_unasked_result_answers_nothing() {
    let mut protocol = EntitlementRefreshProtocol::default();
    assert_eq!(
        protocol.deliver_fetch_result(Some(&summary(false)), NOW),
        EntitlementFetchVerdict::UnexpectedResult
    );
    assert!(protocol.summary().is_none());
}

#[test]
fn clearing_drops_the_summary_and_orphans_the_fetch_in_flight() {
    let mut protocol = EntitlementRefreshProtocol::default();
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::Bootstrap, NOW).is_some());
    protocol.deliver_fetch_result(Some(&summary(false)), NOW);
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::SignIn, NOW + 1).is_some());
    protocol.clear();
    assert!(protocol.summary().is_none());
    assert!(protocol.display().is_none());
    // The mint still running belongs to the account that signed out; its
    // late result installs nothing for whoever signs in next.
    assert_eq!(
        protocol.deliver_fetch_result(Some(&summary(false)), NOW + 2),
        EntitlementFetchVerdict::UnexpectedResult
    );
    // And the next sign-in starts from nothing, immediately.
    assert!(plan(&mut protocol, wire::EntitlementFetchReason::SignIn, NOW + 3).is_some());
}
