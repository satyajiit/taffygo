// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The storage plane's inbound terminal, crossing from Chromium into Rust.
//!
//! The operation record mirrors `BridgeOperation` field for field. Two cxx
//! bridge modules cannot share a by-value struct without an include cycle
//! between their generated headers, so the terminal owns this plane-specific
//! shape and `service_bridge_runtime.rs` converts it exactly once.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeStorageCompletionOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeStorageCompletion {
        operation: BridgeStorageCompletionOperation,
        effect_id: String,
        status: u8,
        committed_revision: u64,
    }
}
