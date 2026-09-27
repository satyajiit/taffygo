// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A connected provider's own model list, across the CXX boundary
//! (decision 0098).
//!
//! The served catalog's two moments, in the same order and for the same
//! reasons: the core plans at most one fetch, the browser carries it out, and
//! the ordered core alone decides what came back means. Read this file beside
//! `service_bridge_catalog.rs`; where the two differ, the difference is the
//! decision rather than the filing.
//!
//! They differ in exactly one place, and it is which way the plan travels. A
//! catalog fetch is *asked for* — the browser pokes and the answer rides the
//! reply — because it is anonymous and belongs to no command. A listing is
//! per-account and becomes possible at a moment the browser cannot see: the
//! moment a person's credential for that provider goes on file. So it is
//! *offered*, on the response to the provider write that made it possible, and
//! reaches the browser through `EmitEffect` like every other effect a command
//! produces. Planning is a pure function of what is stored, so re-planning
//! after a provider write is not a second decision; it is the one decision,
//! taken again with newer facts — the same argument the delivery plane's
//! re-plan carries.
//!
//! Nothing here re-plans on delivery, and that is deliberate. A build with no
//! fetcher answers every listing `UNAVAILABLE`, which confirms nothing, so a
//! delivery that planned again would ask for the same list forever at the
//! speed of the pipe. The next provider write asks again, and that is soon
//! enough for a list a person cannot yet be shown.

use core_runtime::provider_listing::ProviderListingVerdict;
use core_runtime::wire;

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

/// Flattens one planned listing fetch for the flat CXX record.
///
/// `None` when the envelope carries no listing body, which the composition
/// cannot produce: the caller hands this what `plan_provider_listing_refresh`
/// built. A malformed envelope drops the effect rather than panicking a
/// process that carries other profiles' work, exactly as the catalog's
/// flattening does.
fn listing_effect_to_ffi(envelope: wire::EffectEnvelope) -> Option<ffi::BridgeListingEffect> {
    let fetch = envelope.provider_listing_fetch?;
    Some(ffi::BridgeListingEffect {
        operation: ffi::BridgeOperation {
            operation_id: envelope.operation.operation_id,
            service_generation: envelope.operation.service_generation,
            task_revision: envelope.operation.task_revision,
            deadline_monotonic_ms: envelope.operation.deadline_monotonic_ms,
            idempotency_key: envelope.operation.idempotency_key,
        },
        effect_id: envelope.effect_id,
        provider_id: fetch.provider_id,
        endpoint: fetch.endpoint,
        wire_api: fetch.wire_api as u8,
        has_credential_handle: fetch.credential_handle.is_some(),
        credential_handle: fetch.credential_handle.unwrap_or_default(),
        max_response_bytes: fetch.max_response_bytes,
    })
}

/// The listing fetches the plane wants carried out right now.
///
/// At most one, because the protocol keeps one in flight at a time. A profile
/// with no connected provider whose row says its models are served plans
/// nothing, which is the ordinary answer and not a failure.
pub(crate) fn planned_effects(
    bridge: &mut ServiceBridge,
    now_utc_millis: u64,
) -> Vec<ffi::BridgeListingEffect> {
    let Some(runtime) = bridge.runtime.as_mut() else {
        return Vec::new();
    };
    runtime.set_utc_millis(now_utc_millis);
    runtime
        .plan_provider_listing_refresh(now_utc_millis)
        .and_then(listing_effect_to_ffi)
        .into_iter()
        .collect()
}

#[allow(non_snake_case)]
pub(crate) fn DeliverProviderListingResult(
    bridge: &mut ServiceBridge,
    result: ffi::BridgeListingResult,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = result.operation.operation_id.clone();
    let Some(disposition) = wire::CatalogFetchDisposition::from_wire(u32::from(result.disposition))
    else {
        return response(&operation_id, invalid(), Vec::new());
    };
    // The contract: a body rides only a SUCCESS, and a partial one never.
    if disposition != wire::CatalogFetchDisposition::Success && !result.body.is_empty() {
        return response(&operation_id, invalid(), Vec::new());
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    runtime.set_utc_millis(now_utc_millis);
    let wire_result = wire::ProviderListingFetchResult {
        provider_id: result.provider_id,
        disposition,
        body: result.body,
    };
    let verdict = runtime.deliver_provider_listing_result(&wire_result, now_utc_millis);
    if matches!(verdict, ProviderListingVerdict::Accepted { .. }) {
        // Acceptance rebuilt the merge, so this provider's models are in the
        // roster every picker draws from. This publication is the only way a
        // surface ever hears it: the delivery-bridge lesson is that an
        // unpublished state change is invisible rather than wrong.
        return response_after_change(
            bridge,
            response(
                &operation_id,
                wire::AdmissionStatus::Accepted as u8,
                Vec::new(),
            ),
        );
    }
    // A result nothing asked for answers nothing. Every other verdict is a
    // delivered answer that changed no published state — refused, unreadable,
    // not modified, or an endpoint this build could not reach — and each of
    // them leaves the models a person could already reach exactly where they
    // were, which is what decision 0098 requires of a refusal.
    let status = if matches!(verdict, ProviderListingVerdict::UnexpectedResult) {
        invalid()
    } else {
        wire::AdmissionStatus::Accepted as u8
    };
    response(&operation_id, status, Vec::new())
}

const fn invalid() -> u8 {
    wire::AdmissionStatus::InvalidCommand as u8
}
