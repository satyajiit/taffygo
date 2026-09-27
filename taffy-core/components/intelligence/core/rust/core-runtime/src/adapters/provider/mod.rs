// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The provider subsystem's adapters: credential and endpoint installation,
//! and the provider command runtime.

pub mod install;
pub mod provider_commands;

pub use install::{
    credential_metadata, install_provider_state, local_endpoints, model_policy, router_auth_type,
};
pub use provider_commands::{apply_provider_command, ProviderServiceError, ProviderServiceStep};
