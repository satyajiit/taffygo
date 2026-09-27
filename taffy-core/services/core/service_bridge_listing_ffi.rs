// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The served listing's terminal, crossing from Chromium into Rust
//! (decision 0098).
//!
//! Only the inbound half lives here. The *effect* this answers is held in a
//! `Vec` by `BridgeResponse`, so it stays with the shared vocabulary in
//! `service_bridge_state_ffi.rs`, where defining it instantiates that vector
//! once. That is the same split the key probe already has.
//!
//! The operation record mirrors `BridgeOperation` field for field, the same way
//! the account, composer, policy, provider and task-terminal bridges
//! mirror it: two cxx bridge modules cannot share a by-value struct without an
//! include cycle between their generated headers, so the shape is duplicated
//! under the plane's own name and `service_bridge_listing.rs` converts it
//! exactly once.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeListingOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    /// What the browser's listing fetch observed, flattened for the bridge.
    ///
    /// This was field for field with the served catalog's `BridgeCatalogFetchResult`
    /// apart from the identity it names, which was decision 0098 rather than
    /// convenience: the same dispositions mean the same things, because the
    /// same things happen to a listing as to a fetched document. That
    /// counterpart left with the served catalog (decision 0200) and the shape
    /// is unchanged, because it was never the catalog's — it is what a fetch
    /// observes. `provider_id` is what says whose list this is, and the
    /// ordered core answers a provider it has no fetch out for as an answer to
    /// a question nobody asked.
    ///
    /// A build with no fetcher answers `UNAVAILABLE` with an empty body, which
    /// the core reads as a transport failure and never as an empty catalog —
    /// the last consequence of decision 0098, kept true by the disposition
    /// rather than by anyone remembering it.
    struct BridgeListingResult {
        operation: BridgeListingOperation,
        effect_id: String,
        provider_id: String,
        disposition: u8,
        body: Vec<u8>,
    }
}
