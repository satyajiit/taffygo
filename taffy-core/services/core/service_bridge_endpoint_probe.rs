// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The custom-endpoint probe's two legs across the CXX boundary
//! (decision 0096).
//!
//! Its sibling `service_bridge_probe.rs` proves a *credential* against a
//! provider the catalog already carries. This proves an *address*: what
//! answers at something a person typed, before anything is saved under it. The
//! two share one flight and one row on the status projection and nothing else,
//! which is why they are two files rather than one — the key probe composes a
//! bounded model call, and this composes a fetch the browser's prober performs.
//!
//! Both legs publish, and neither is optional about it. The claimed flight is
//! what a setup sheet draws while it waits, and the verdict is what it draws
//! afterwards; both live on `CoreStatus`, and `CoreStatus` reaches a surface
//! only through `response_after_change`. A leg here that returned a bare
//! response would leave a person watching a sheet that never stops saying it
//! is testing — the delivery-bridge failure, on a screen a person is looking
//! straight at.

use core_runtime::wire;

use crate::ffi;
use crate::service_bridge_provider::{custom_model_to_wire, probe_error_status};
use crate::service_bridge_provider_ffi::ffi as provider_ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

/// Admits one probe of an address a person typed.
///
/// Called by `SubmitProvider`, which owns the provider plane's command record
/// and has already checked the operation envelope. The composition claims the
/// single flight and hands back the fetch to perform; the verdict arrives
/// later through [`DeliverEndpointProbeResult`].
pub(crate) fn submit(
    bridge: &mut ServiceBridge,
    command: &wire::CoreServiceCommand,
    operation_id: &str,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(operation_id, unavailable(), Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);
    match runtime.submit_custom_endpoint_probe_command(command) {
        Ok(effect) => {
            let mut accepted = response(
                operation_id,
                wire::AdmissionStatus::Accepted as u8,
                Vec::new(),
            );
            accepted.endpoint_probe_effects = vec![effect_to_bridge(effect)];
            // The claimed flight is the row a sheet draws while it waits, so
            // an accepted probe publishes for the same reason the key probe's
            // does.
            response_after_change(bridge, accepted)
        }
        Err(error) => response(operation_id, probe_error_status(error), Vec::new()),
    }
}

#[allow(non_snake_case)]
pub(crate) fn DeliverEndpointProbeResult(
    bridge: &mut ServiceBridge,
    result: provider_ffi::BridgeEndpointProbeResult,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = result.operation.operation_id.clone();
    let effect_id = result.effect_id.clone();
    let Some(verdict) = result_to_wire(result) else {
        return response(&operation_id, invalid(), Vec::new());
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(&operation_id, unavailable(), Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);
    let filed =
        runtime.deliver_custom_endpoint_probe_result(&effect_id, &verdict, now_monotonic_ms);
    let accepted = response(
        &operation_id,
        wire::AdmissionStatus::Accepted as u8,
        Vec::new(),
    );
    if filed {
        // The verdict is what the setup sheet draws instead of "Testing…", and
        // this publication is the only way it ever hears it.
        response_after_change(bridge, accepted)
    } else {
        // A late or foreign terminal answers nothing that was asked; nothing
        // changed and nothing publishes.
        accepted
    }
}

/// Flattens the composed probe fetch for the flat CXX record.
///
/// Total rather than fallible, exactly as the key probe's flattening is and
/// for the same reason: the caller hands this the envelope
/// `submit_custom_endpoint_probe_command` composed, whose probe body is always
/// present. A malformed envelope flattens to empty fields the browser's
/// validation then refuses whole, rather than panicking a process that carries
/// other profiles' work.
fn effect_to_bridge(envelope: wire::EffectEnvelope) -> ffi::BridgeEndpointProbeEffect {
    let probe = envelope
        .custom_endpoint_probe
        .unwrap_or(wire::CustomEndpointProbeEffect {
            provider_id: String::new(),
            endpoint: String::new(),
            wire_api: wire::ProviderWireApi::AnthropicMessages,
            credential_handle: None,
            max_response_bytes: 0,
        });
    ffi::BridgeEndpointProbeEffect {
        operation: ffi::BridgeOperation {
            operation_id: envelope.operation.operation_id,
            service_generation: envelope.operation.service_generation,
            task_revision: envelope.operation.task_revision,
            deadline_monotonic_ms: envelope.operation.deadline_monotonic_ms,
            idempotency_key: envelope.operation.idempotency_key,
        },
        effect_id: envelope.effect_id,
        provider_id: probe.provider_id,
        endpoint: probe.endpoint,
        wire_api: probe.wire_api as u8,
        has_credential_handle: probe.credential_handle.is_some(),
        credential_handle: probe.credential_handle.unwrap_or_default(),
        max_response_bytes: probe.max_response_bytes,
    }
}

/// The flat CXX record, back into the contract's own verdict.
///
/// The bounds are applied here as well as inside the composition, because this
/// is where a list the browser's prober assembled enters the core and the
/// contract's limits are the product's rather than the caller's. `model_count`
/// is not held to the list's length and must not be: the count is what the
/// server named and the list is what survived the bound, and reading one as
/// the other is the silent truncation decision 0096 section 5 refuses.
fn result_to_wire(
    value: provider_ffi::BridgeEndpointProbeResult,
) -> Option<wire::CustomEndpointProbeResult> {
    if value.provider_id.is_empty()
        || value.provider_id.len() > wire::MAX_PROVIDER_ID_BYTES
        || value.models.len() > wire::MAX_CUSTOM_MODEL_ENTRIES
        || value.proved_base.len() > wire::MAX_PROVIDER_ENDPOINT_BYTES
        || (!value.has_proved_base && !value.proved_base.is_empty())
    {
        return None;
    }
    // Absent, never a member standing for absence. The contract wraps the
    // enumeration in a record precisely so that "the server named no runtime"
    // has no member of its own, and a projection that invented one would be
    // answering a question the prober did not.
    let detected_server = if value.has_detected_server {
        Some(wire::DetectedServer {
            server_kind: wire::ServerKind::from_wire(u32::from(value.detected_server))?,
        })
    } else {
        None
    };
    Some(wire::CustomEndpointProbeResult {
        provider_id: value.provider_id,
        reached: value.reached,
        detected_server,
        model_count: value.model_count,
        models: value.models.into_iter().map(custom_model_to_wire).collect(),
        proved_base: value.has_proved_base.then_some(value.proved_base),
    })
}

const fn invalid() -> u8 {
    wire::AdmissionStatus::InvalidCommand as u8
}

const fn unavailable() -> u8 {
    wire::AdmissionStatus::CoreUnavailable as u8
}
