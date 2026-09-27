// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The operation envelope every published effect names.
//!
//! One record in a bridge of its own, because it is the root of the alias
//! graph: `BridgeTaskEffect` holds it by value, every effect vector in a
//! `BridgeResponse` holds it by value, and the root bridge takes it by value
//! from the browser. A cxx bridge may hold a record another bridge defines
//! when that record is only ever a by-value field, element or argument —
//! cxx emits `ExternType<Kind = Trivial>` for every shared struct, and a
//! trivial extern alias is by-value material — so the envelope is defined
//! exactly once here and named back through `type BridgeOperation =
//! crate::service_bridge_operation_ffi::ffi::BridgeOperation;` wherever a
//! bridge carries it. This module includes nothing, which is what keeps the
//! generated headers a DAG: operation, then task effect, then state, then the
//! planes that wrap a response, then the root.
//!
//! The per-plane inbound records (`BridgeCatalogOperation` and its peers)
//! still mirror this shape under their own names. That is a choice those
//! modules make so that an inbound record converts exactly once in its own
//! projection; it is not a constraint of cxx.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }
}
