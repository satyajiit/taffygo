// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One durable reducer command crossing from Chromium into Rust.
//!
//! The operation record mirrors `BridgeOperation` field for field, the same
//! way the account, composer, policy, provider and task-terminal bridges
//! mirror it: two cxx bridge modules cannot share a by-value struct without an
//! include cycle between their generated headers, so the shape is duplicated
//! under the plane's own name and `service_bridge_task.rs` reads it exactly
//! once on its way to the wire envelope.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeTaskCommandOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeTaskCommand {
        operation: BridgeTaskCommandOperation,
        kind: u8,
        task_id: String,
        cancel_reason: u8,
        action_id: String,
        user_decision: u8,
        approval_digest: String,
        trace_id: String,
        approval_receipt_id: String,
        approval_expires_at_monotonic_ms: u64,
        approval_expires_at_utc_ms: u64,
        browser_session_id: String,
        request_id: String,
        permission: u8,
        permission_decision: u8,
        handover_id: String,
        lease_before: String,
        resumed_with: String,
        person_input: u32,
        answer: String,
        supplied: u32,
        /// What became of a request for values, as `FieldValueAskOutcome`'s
        /// wire number (decision 0215).
        ///
        /// A `u8` beside the count and not instead of it: the two are
        /// independent facts, and a browser holding values still holds them
        /// whatever the member says. Read through `FieldValueAskOutcome`'s
        /// own `from_wire`, so an unknown number refuses the crossing rather
        /// than defaulting to `Answered` and telling the task the person
        /// answered when nobody did.
        supplied_outcome: u8,
        /// The field each held value was minted for, by position, as
        /// observation node ids and never values (decision 0238).
        ///
        /// Exactly `supplied` entries, distinct, each a bounded identifier;
        /// anything else refuses the crossing. The core fills value `i` into
        /// entry `i` with no model turn between.
        supplied_field_node_ids: Vec<String>,
        artifact_id: String,
        artifact_kind: u8,
    }
}
