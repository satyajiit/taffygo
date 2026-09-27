// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Redacted debug projections for profile runtime records.

use super::{ProfileRuntimeConfiguration, ProfileServiceRuntime};

impl core::fmt::Debug for ProfileRuntimeConfiguration {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("ProfileRuntimeConfiguration")
            .field("generation", &self.generation)
            .field("initial_utc_millis", &self.initial_utc_millis)
            .field("private_profile", &self.private_profile)
            .field("browser_profile_id", &self.browser_profile_id)
            .field("browser_session_id", &self.browser_session_id)
            .field("available_account_methods", &self.available_account_methods)
            .finish_non_exhaustive()
    }
}

impl core::fmt::Debug for ProfileServiceRuntime {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("ProfileServiceRuntime")
            .field("core", &self.core)
            .finish_non_exhaustive()
    }
}
