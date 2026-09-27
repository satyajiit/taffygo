// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The key probe's delivery leg across the CXX boundary (decision 0083).
//!
//! The probe's effect travels out on `SubmitProvider`'s response, flattened by
//! [`probe_effect_to_bridge`]; this file owns what comes back. The browser
//! reports the one dispatch's terminal — the coarse effect status and the
//! provider's own HTTP status — and the ordered core alone classifies it into
//! the verdict the status projection carries. A filed verdict publishes on
//! this same answer, because the verdict is surface state and an unpublished
//! state change is invisible rather than wrong (the delivery-bridge lesson).

use core_runtime::probe::ProbeOutcome;
use core_runtime::wire;

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

/// Flattens the probe's composed effect for the flat CXX record.
///
/// Field for field, and probe-only by construction: the caller hands this the
/// envelope `submit_probe_command` composed, whose model request is always
/// present, always task-less and always marked `probe`. A malformed envelope
/// flattens to empty fields the browser's validation then refuses whole,
/// rather than panicking a process that carries other profiles' work.
pub(crate) fn probe_effect_to_bridge(envelope: wire::EffectEnvelope) -> ffi::BridgeProbeEffect {
    let request = envelope.model_request.unwrap_or(wire::ModelRequestEffect {
        route_id: String::new(),
        model_id: String::new(),
        disclosure: wire::DisclosureClass::ContentFree,
        request_body: Vec::new(),
        max_output_bytes: 0,
        task_id: String::new(),
        provider_id: String::new(),
        wire_api: wire::ProviderWireApi::AnthropicMessages,
        endpoint: String::new(),
        credential_handle: None,
        static_headers: Vec::new(),
        probe: true,
        endpoint_kind: wire::ModelEndpointKind::CatalogOrigin,
        media_attachment_handle: None,
        media_attachment_mime_type: None,
        not_before_monotonic_ms: 0,
    });
    ffi::BridgeProbeEffect {
        operation: ffi::BridgeOperation {
            operation_id: envelope.operation.operation_id,
            service_generation: envelope.operation.service_generation,
            task_revision: envelope.operation.task_revision,
            deadline_monotonic_ms: envelope.operation.deadline_monotonic_ms,
            idempotency_key: envelope.operation.idempotency_key,
        },
        effect_id: envelope.effect_id,
        retry_class: envelope.retry_class as u8,
        route_id: request.route_id,
        model_id: request.model_id,
        disclosure: request.disclosure as u8,
        request_body: request.request_body,
        max_output_bytes: request.max_output_bytes,
        provider_id: request.provider_id,
        wire_api: request.wire_api as u8,
        endpoint: request.endpoint,
        credential_handle: request.credential_handle.unwrap_or_default(),
    }
}

#[allow(non_snake_case)]
pub(crate) fn DeliverProbeCompletion(
    bridge: &mut ServiceBridge,
    completion: ffi::BridgeProbeCompletion,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let effect_id = completion.effect_id;
    let Some(status) = wire::EffectStatus::from_wire(u32::from(completion.status)) else {
        return response(
            &effect_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &effect_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    runtime.set_utc_millis(now_utc_millis);
    let filed = runtime.deliver_probe_result(
        &effect_id,
        ProbeOutcome {
            status,
            provider_http_status: completion.provider_http_status,
        },
        now_monotonic_ms,
    );
    let accepted = response(
        &effect_id,
        wire::AdmissionStatus::Accepted as u8,
        Vec::new(),
    );
    if filed {
        // The verdict is surface state, and this publication is the only way
        // a sheet waiting on "Testing…" ever hears it.
        response_after_change(bridge, accepted)
    } else {
        // A late or foreign terminal answers nothing that was asked; nothing
        // changed and nothing publishes.
        accepted
    }
}
