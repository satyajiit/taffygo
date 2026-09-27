// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The loop kernel: the machinery around the durable reducer (decision 0072).
//!
//! The reducer decides; this crate owns what surrounds the decision — the
//! scheduler walk, model-turn composition and reading, the person-answer
//! classification, and the bounded transient context one task holds while it
//! runs. Everything here is pure given its arguments: no clock, no socket, no
//! ambient state. Effects are performed above, by the browser, and return as
//! typed completions.
//!
//! The crate is visible only to `core-runtime`, which implements the narrow
//! ports declared here and re-exports these names at their historical paths
//! for the service bridge.
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

pub mod context;
pub mod digest;
mod native_table;
pub mod ports;
pub mod provider;
pub mod recording;
pub mod state;
pub mod turn;
pub mod walk;
pub mod window;
