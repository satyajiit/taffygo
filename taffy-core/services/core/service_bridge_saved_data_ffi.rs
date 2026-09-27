// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Saved-data-only inbound CXX records.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeSavedDataOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeSavedSignIn {
        id: String,
        site: String,
        username: String,
        last_used_epoch_ms: u64,
    }

    struct BridgeSavedDetail {
        id: String,
        given_name: String,
        family_name: String,
        email: String,
        phone: String,
        address: String,
        postcode: String,
        country: String,
    }

    struct BridgeSavedDataCommand {
        operation: BridgeSavedDataOperation,
        sign_ins_availability: u8,
        sign_ins_revision: u64,
        sign_ins: Vec<BridgeSavedSignIn>,
        details_availability: u8,
        details_revision: u64,
        details: Vec<BridgeSavedDetail>,
    }
}
