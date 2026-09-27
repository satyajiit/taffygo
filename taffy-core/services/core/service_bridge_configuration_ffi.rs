// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! CXX records for durable one-assistant configuration commands.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeConfigurationOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeAssistantConfigurationCommand {
        operation: BridgeConfigurationOperation,
        expected_revision: u64,
        disabled_abilities: Vec<u8>,
        preset: u8,
        pace: u32,
        length: u32,
        check_in: u32,
    }
}
