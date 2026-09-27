// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exactly one canonical `Production*` adapter per port, one subsystem per
//! directory. Adapters are constructed only by [`crate::composition`].

pub mod account;
pub mod assets;
pub mod audit;
pub mod crosscutting;
pub mod ids;
pub mod library;
pub mod memory;
pub mod model;
pub mod persistence;
pub mod policy;
pub mod provider;
pub mod task;
pub mod time;
pub mod workspace;
