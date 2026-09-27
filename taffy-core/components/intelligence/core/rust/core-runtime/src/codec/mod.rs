// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Boundary codecs: fail-closed conversions between generated contract types
//! and the crate's own vocabulary. A codec decides nothing; it decodes,
//! validates shape, and refuses by name.

pub mod account_wire;
pub mod action_result_code;
pub mod completion_wire;
pub mod grant_wire;
pub mod observation_wire;
pub mod policy_wire;
pub mod start_shape;
pub mod start_task;
