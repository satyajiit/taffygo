// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The account subsystem's adapters: the port adapter and the operation
//! runtime that drives one account flow at a time.

pub mod account_runtime;
pub mod adapter;

pub use account_runtime::{AccountOperationRuntime, AccountServiceError, AccountServiceStep};
pub use adapter::ProductionAccount;
