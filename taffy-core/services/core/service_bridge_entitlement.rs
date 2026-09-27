// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The managed entitlement's two bridge legs (decision 0082).
//!
//! Planning answers a browser poke with at most one fetch effect, and
//! delivery hands the mint's outcome to the ordered core, which alone decides
//! what it means. The summary crosses this seam already parsed — the browser
//! read its own worker's response and kept the token it arrived beside — and
//! the record it crosses in has no field a bearer credential could ride, so
//! "the token stays in the browser process" is a property of the seam rather
//! than a discipline of its callers.
//!
//! An installed summary changes what routing may spend and what the account
//! surface shows, and the publication riding the delivery answer is the only
//! way a surface hears either (the delivery-bridge lesson: an unpublished
//! change is invisible rather than wrong).

use core_runtime::entitlement_refresh::EntitlementFetchVerdict;
use core_runtime::wire;

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

/// The no-fetch answer. cxx structs have no optional, so the plan's flag is
/// what carries "nothing is due" and these fields carry nothing.
fn empty_effect() -> ffi::BridgeEntitlementFetchEffect {
    ffi::BridgeEntitlementFetchEffect {
        operation: ffi::BridgeEntitlementOperation {
            operation_id: String::new(),
            service_generation: 0,
            task_revision: 0,
            deadline_monotonic_ms: 0,
            idempotency_key: String::new(),
        },
        effect_id: String::new(),
        reason: 0,
        max_response_bytes: 0,
    }
}

fn no_plan() -> ffi::BridgeEntitlementPlan {
    ffi::BridgeEntitlementPlan {
        has_effect: false,
        effect: empty_effect(),
    }
}

fn entitlement_effect_to_ffi(
    envelope: wire::EffectEnvelope,
) -> Option<ffi::BridgeEntitlementFetchEffect> {
    let network = envelope.network_request?;
    let fetch = network.fetch_entitlement?;
    Some(ffi::BridgeEntitlementFetchEffect {
        operation: ffi::BridgeEntitlementOperation {
            operation_id: envelope.operation.operation_id,
            service_generation: envelope.operation.service_generation,
            task_revision: envelope.operation.task_revision,
            deadline_monotonic_ms: envelope.operation.deadline_monotonic_ms,
            idempotency_key: envelope.operation.idempotency_key,
        },
        effect_id: envelope.effect_id,
        reason: fetch.reason as u8,
        max_response_bytes: network.max_response_bytes,
    })
}

#[allow(non_snake_case)]
pub(crate) fn PlanEntitlementRefresh(
    bridge: &mut ServiceBridge,
    reason: u8,
    now_utc_millis: u64,
) -> ffi::BridgeEntitlementPlan {
    let Some(reason) = wire::EntitlementFetchReason::from_wire(u32::from(reason)) else {
        return no_plan();
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return no_plan();
    };
    runtime.set_utc_millis(now_utc_millis);
    match runtime
        .plan_entitlement_refresh(reason, now_utc_millis)
        .and_then(entitlement_effect_to_ffi)
    {
        Some(effect) => ffi::BridgeEntitlementPlan {
            has_effect: true,
            effect,
        },
        None => no_plan(),
    }
}

/// Whether a flattened summary honours the contract's own bounds.
///
/// The generated codec enforces these on every encoded record; a record built
/// field-by-field from bridge data has to be held to the same line here, or
/// this seam would be the one place an oversized value can enter the core.
fn valid_summary(summary: &ffi::BridgeEntitlementSummary) -> bool {
    summary.plan_id.len() <= wire::MAX_IDENTIFIER_BYTES
        && summary.model_ids.len() <= wire::MAX_ENTITLED_MODELS
        && summary
            .model_ids
            .iter()
            .all(|model_id| !model_id.is_empty() && model_id.len() <= wire::MAX_IDENTIFIER_BYTES)
        && summary.worker_host.len() <= wire::MAX_PROVIDER_ENDPOINT_BYTES
        && summary.gateway_host.len() <= wire::MAX_PROVIDER_ENDPOINT_BYTES
}

fn summary_to_wire(summary: ffi::BridgeEntitlementSummary) -> wire::EntitlementSummaryResult {
    wire::EntitlementSummaryResult {
        definitive_absent: summary.definitive_absent,
        plan_id: summary.plan_id,
        model_ids: summary.model_ids,
        window_seconds: summary.window_seconds,
        requests_remaining: summary.requests_remaining,
        credits_granted: summary.credits_granted,
        credits_remaining: summary.credits_remaining,
        credit_unit_micros: summary.credit_unit_micros,
        next_renewal_epoch_seconds: summary.next_renewal_epoch_seconds,
        valid_until_epoch_seconds: summary.valid_until_epoch_seconds,
        minted_at_utc_ms: summary.minted_at_utc_ms,
        worker_host: summary.worker_host,
        gateway_host: summary.gateway_host,
    }
}

#[allow(non_snake_case)]
pub(crate) fn DeliverEntitlementFetchResult(
    bridge: &mut ServiceBridge,
    result: ffi::BridgeEntitlementFetchResult,
    now_utc_millis: u64,
) -> ffi::BridgeEntitlementDelivery {
    let operation_id = result.operation.operation_id.clone();
    if result.has_summary && !valid_summary(&result.summary) {
        return refused(&operation_id, wire::AdmissionStatus::InvalidCommand as u8);
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return refused(&operation_id, wire::AdmissionStatus::CoreUnavailable as u8);
    };
    runtime.set_utc_millis(now_utc_millis);
    let summary = result.has_summary.then(|| summary_to_wire(result.summary));
    let verdict = runtime.deliver_entitlement_fetch_result(summary.as_ref(), now_utc_millis);
    if matches!(verdict, EntitlementFetchVerdict::Installed { .. }) {
        // Installation changed the routes and the account surface's plan row,
        // and this publication is the only way either is ever heard.
        return ffi::BridgeEntitlementDelivery {
            installed: true,
            response: response_after_change(
                bridge,
                response(
                    &operation_id,
                    wire::AdmissionStatus::Accepted as u8,
                    Vec::new(),
                ),
            ),
        };
    }
    // A result nothing asked for answers nothing; a transport failure is a
    // delivered answer that changed no published state — the protocol kept
    // what it had, and a later poke may plan again.
    let status = if matches!(verdict, EntitlementFetchVerdict::UnexpectedResult) {
        wire::AdmissionStatus::InvalidCommand as u8
    } else {
        wire::AdmissionStatus::Accepted as u8
    };
    refused(&operation_id, status)
}

/// A delivery answer that installs nothing and publishes no state.
fn refused(operation_id: &str, status: u8) -> ffi::BridgeEntitlementDelivery {
    ffi::BridgeEntitlementDelivery {
        installed: false,
        response: response(operation_id, status, Vec::new()),
    }
}
