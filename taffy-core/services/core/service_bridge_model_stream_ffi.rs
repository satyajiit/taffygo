// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The bounded model-stream exchange crossing between Chromium and Rust.
//!
//! The operation record mirrors `BridgeOperation` field for field. The
//! delivery has a dedicated answer-event carrier because it is synchronous
//! backpressure, not a published state response. Keeping it here prevents the
//! shared state bridge from owning a second plane's request and response.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeModelStreamOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeModelStreamChunk {
        operation: BridgeModelStreamOperation,
        effect_id: String,
        sequence: u32,
        data: Vec<u8>,
    }

    struct BridgeModelStreamAnswerEvent {
        task_id: String,
        call_id: String,
        sequence: u32,
        has_text: bool,
        text: String,
        terminal: bool,
        complete: bool,
    }

    struct BridgeModelStreamDelivery {
        status: u8,
        answer_events: Vec<BridgeModelStreamAnswerEvent>,
    }
}
