// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The key probe's terminal, crossing from Chromium into Rust (decision 0083).
//!
//! No operation record is mirrored here, and that is the record rather than an
//! omission: a probe terminal names the effect it answers and nothing else.
//! The probe *effect* is not here either — a response carries it, so it stays
//! with the rest of the published vocabulary.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    /// The terminal of one probe dispatch, as the browser observed it: the
    /// coarse effect status, and the provider's own HTTP status when the
    /// provider was reached (zero when it was not).
    struct BridgeProbeCompletion {
        effect_id: String,
        status: u8,
        provider_http_status: u32,
    }
}
