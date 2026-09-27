// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Delivery-plane inbound CXX records kept out of the shared state bridge.
//!
//! The operation record mirrors `BridgeOperation` field for field, the same
//! way the account, composer, policy, provider and task-terminal bridges
//! mirror it: two cxx bridge modules cannot share a by-value struct without an
//! include cycle between their generated headers, so the shape is duplicated
//! under the plane's own name and `service_bridge_assets.rs` converts it
//! exactly once. The delivery *effect* is not here — a response and a
//! bootstrap's plan both carry it, so it stays with the rest of the published
//! vocabulary.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeAssetOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    /// One command about the product's own artifacts.
    ///
    /// Flat rather than tagged, like every other command record here: cxx has
    /// no sum type, so the kind names which fields carry meaning and the
    /// projection refuses a body that does not match it.
    struct BridgeAssetCommand {
        operation: BridgeAssetOperation,
        kind: u8,
        network_cost: u8,
        metered_permitted: bool,
        asset_id: String,
        asset_revision: String,
    }

    /// What the browser observed carrying one delivery effect out.
    struct BridgeAssetReport {
        operation: BridgeAssetOperation,
        effect_id: String,
        operation_kind: u8,
        asset_id: String,
        asset_revision: String,
        outcome: u8,
        written_bytes: u64,
        observed_bytes: u64,
        observed_digest: [u8; 32],
        reclaimed_bytes: u64,
    }
}
