// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The managed entitlement's own CXX records (decision 0082).
//!
//! All three legs of the plane live here — the mint it plans, the summary it
//! is handed, and the delivery's answer. The answer wraps a `BridgeResponse`
//! by value, named from the state bridge through a trivial extern alias: cxx
//! emits `ExternType<Kind = Trivial>` for every shared struct, so a record
//! another bridge defines may be a by-value field here exactly as it may be a
//! by-value return in the root bridge. This header includes the state bridge's
//! and nothing includes this one but the root, which keeps the generated
//! headers a DAG.
//!
//! The operation record mirrors `BridgeOperation` field for field, the same
//! way the account, composer, policy, provider and task-terminal bridges
//! mirror it. That is this plane's choice rather than a constraint of cxx:
//! an inbound record converts exactly once, in
//! `service_bridge_entitlement.rs`, and a mirrored envelope keeps that
//! conversion in one place.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    unsafe extern "C++" {
        include!("taffy/services/core/service_bridge_state_ffi.rs.h");

        type BridgeResponse = crate::service_bridge_state_ffi::ffi::BridgeResponse;
    }

    struct BridgeEntitlementOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    /// One entitlement fetch the ordered core wants performed (decision
    /// 0082).
    ///
    /// No URL, no token and no account identity: the browser resolves the
    /// operation against its compiled worker origin and its own signed-in
    /// session, and the token the mint produces never crosses this seam in
    /// either direction.
    struct BridgeEntitlementFetchEffect {
        operation: BridgeEntitlementOperation,
        effect_id: String,
        reason: u8,
        max_response_bytes: u32,
    }

    /// At most one planned entitlement fetch. cxx has no optional, so the
    /// flag says whether the effect carries meaning.
    struct BridgeEntitlementPlan {
        has_effect: bool,
        effect: BridgeEntitlementFetchEffect,
    }

    /// The mint's summary, flattened for the bridge. Counts, identifiers and
    /// hosts only — the contract record it becomes has no field a bearer
    /// credential could ride, and neither does this one.
    struct BridgeEntitlementSummary {
        definitive_absent: bool,
        plan_id: String,
        model_ids: Vec<String>,
        window_seconds: u32,
        requests_remaining: u64,
        credits_granted: u64,
        credits_remaining: u64,
        credit_unit_micros: u64,
        next_renewal_epoch_seconds: u64,
        valid_until_epoch_seconds: u64,
        minted_at_utc_ms: u64,
        worker_host: String,
        gateway_host: String,
    }

    /// What the browser's mint observed. `has_summary` false is a transport
    /// failure — the worker was not definitively heard — and the summary
    /// fields then carry nothing.
    struct BridgeEntitlementFetchResult {
        operation: BridgeEntitlementOperation,
        effect_id: String,
        has_summary: bool,
        summary: BridgeEntitlementSummary,
    }

    /// The delivery's answer, beside the one fact the browser acts on:
    /// whether a summary was installed and published.
    struct BridgeEntitlementDelivery {
        installed: bool,
        response: BridgeResponse,
    }
}
