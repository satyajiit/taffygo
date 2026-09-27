// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generated, authority-neutral values for the Tool Runtime boundary.
//!
//! The schema and generator live at `taffy-core/contracts/tool-runtime/`.
//! This crate is deliberately only a source-owning wrapper: the browser
//! supervisor and every worker consume the same generated records and never
//! define a second wire shape for a job, a chunk, or a terminal result.
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

// The tagged unions project as structs with optional fields rather than
// data-carrying Rust enums, so this needs no large_enum_variant allowance.
#[rustfmt::skip]
#[path = "../../../generated/rust/tool_runtime.rs"]
mod generated;

pub use generated::*;
