// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Provider-only inbound CXX records kept out of the shared state bridge.
//!
//! The operation record mirrors `BridgeOperation` field for field, the same
//! way the account bridge mirrors it: two cxx bridge modules cannot share a
//! by-value struct without an include cycle between their generated headers,
//! so the shape is duplicated under the plane's own name and the projection
//! in `service_bridge_provider.rs` converts it exactly once.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeProviderOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    /// One model a person's own endpoint said it offers (decision 0096).
    ///
    /// The contract's `CustomModelSpec`, field for field. It describes a model
    /// and nothing about where to reach it: an address is the person's own and
    /// arrives on the command beside this, never once per model, so nothing
    /// here can move one model of a saved provider to another host.
    struct BridgeCustomModel {
        model_id: String,
        display_name: String,
        context_window: u32,
        max_output_tokens: u32,
        reasoning: bool,
        tool_calling: bool,
    }

    /// One command about a person's model providers.
    ///
    /// Flat rather than tagged, like every other command record here: cxx has
    /// no sum type, so `kind` names which fields carry meaning and the
    /// projection refuses a body that does not match it. `credential_handle`
    /// is an opaque browser secure-store reference and never material — no
    /// field on this record can hold a key, which is a property of the type
    /// rather than of the code that fills it (decision 0049).
    struct BridgeProviderCommand {
        operation: BridgeProviderOperation,
        kind: u8,
        provider_id: String,
        auth_method: u8,
        credential_state: u8,
        has_credential_handle: bool,
        credential_handle: String,
        display_name: String,
        endpoint: String,
        wire_api: u8,
        flow_id: String,
        redirect_binding_id: String,
        issued_at_monotonic_ms: u64,
        /// `AuthCallbackStatus` for PROVIDER_AUTH_CALLBACK, else ignored.
        callback_status: u8,
        /// The redirect's echoed state. The browser compared it already; it
        /// crosses here bounded so the seam can refuse a runaway value.
        returned_state: String,
        has_authorization_code_handle: bool,
        /// Opaque one-shot vault handle; never authorization-code material.
        authorization_code_handle: String,
        /// What SAVE_CUSTOM_PROVIDER files behind the endpoint, else ignored.
        ///
        /// It travels on the save rather than after it because a provider
        /// filed with nothing behind it is a row a person can see, select and
        /// never reach; decision 0096 section 4 is that both halves are one
        /// write. Bounded by `MAX_CUSTOM_MODEL_ENTRIES`, refused at the
        /// projection.
        models: Vec<BridgeCustomModel>,
        /// What the probe read the server to be, when it read one.
        ///
        /// The pair is how "nothing was detected" crosses a bridge with no
        /// optional, and it is kept apart from the enumeration for the same
        /// reason the contract wraps a one-field record around it: mojom has
        /// no optional enum, so an absent detection must be the absent record
        /// and never a member meaning "no member".
        has_detected_server: bool,
        detected_server: u8,
        /// The model a person pinned, and whether they pinned one at all.
        ///
        /// The pair is how an absent choice crosses a bridge with no optional.
        /// Both halves of a standing choice carry it as it should now stand,
        /// so an absent model is a cleared pin rather than a field left alone
        /// (decision 0093 section 1).
        has_model_id: bool,
        model_id: String,
        /// The thinking rung a person chose, and whether they chose one.
        ///
        /// A pair for the same reason and one more: absence is "Taffy
        /// decides" while `OFF` is a person asking for no thinking phase, and
        /// the enumeration has no member standing for absence, so a sentinel
        /// would collapse two answers a screen has to keep apart (decision
        /// 0093 section 3).
        has_thinking_level: bool,
        thinking_level: u8,
    }

    /// What answered at an address a person typed (decision 0096 section 5).
    ///
    /// Here rather than in an endpoint-probe module of its own because it
    /// carries `BridgeCustomModel`, and a cxx bridge cannot hold a record
    /// another bridge defines. The plane's projection lives in
    /// `service_bridge_endpoint_probe.rs` and converts this exactly once; the
    /// probe *effect* is not here at all, because a response carries it and so
    /// it belongs with the shared state vocabulary.
    ///
    /// `model_count` and `models` are separate facts and stay separate. The
    /// count is what the server named and the list is what survived
    /// `MAX_CUSTOM_MODEL_ENTRIES`; reading the list's length as the count is
    /// exactly the silent truncation decision 0096 refuses. Zero models is an
    /// answer — the address is right and nothing is loaded behind it.
    ///
    /// `proved_base` is the base the OpenAI-shaped API actually answered at,
    /// which is not always the base that was typed. It travels so that a save
    /// files an address that was proved rather than one that merely looked
    /// reachable.
    struct BridgeEndpointProbeResult {
        operation: BridgeProviderOperation,
        effect_id: String,
        provider_id: String,
        reached: bool,
        has_detected_server: bool,
        detected_server: u8,
        model_count: u32,
        models: Vec<BridgeCustomModel>,
        has_proved_base: bool,
        proved_base: String,
    }
}
