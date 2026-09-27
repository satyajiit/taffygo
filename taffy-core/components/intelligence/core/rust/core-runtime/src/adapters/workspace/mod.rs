// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The workspace subsystem's adapters: the port adapter, the store, and the
//! export encoders.

pub(crate) mod rich_artifact;
pub mod workspace_export;
pub mod workspace_export_time;
pub mod workspace_store;
pub mod workspaces;

pub use workspace_store::WorkspaceStore;
pub use workspaces::ProductionWorkspaces;
