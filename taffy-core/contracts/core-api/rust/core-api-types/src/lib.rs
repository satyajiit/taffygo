// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generated, authority-neutral values for the Core API boundary.
//!
//! The semantic records, payload layout, bounds, and generator live under
//! `taffy-core/contracts/core-api/`. The sandboxed service and browser consume
//! this crate instead of maintaining another state model or byte codec.
#![doc(html_no_source)]
#![cfg_attr(
    test,
    allow(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::indexing_slicing
    )
)]

#[rustfmt::skip]
#[path = "../../../generated/rust/core_api.rs"]
mod generated;

pub use generated::*;

#[rustfmt::skip]
#[path = "../../../generated/rust/core_status_codec.rs"]
mod status_codec;

pub use status_codec::*;
