// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable one-assistant configuration access on the profile runtime.

use crate::assistant_configuration::{AssistantConfiguration, AssistantConfigurationError};

use super::ProfileServiceRuntime;

impl ProfileServiceRuntime {
    /// Current durable configuration for the one assistant.
    pub const fn assistant_configuration(&self) -> &AssistantConfiguration {
        &self.assistant_configuration
    }

    /// Validates a whole-record compare-and-set without publishing it.
    pub fn prepare_assistant_configuration(
        &self,
        command: &core_service_types::SetAssistantConfigurationCommand,
    ) -> Result<AssistantConfiguration, AssistantConfigurationError> {
        self.assistant_configuration.prepare_update(command)
    }

    /// Installs a value only after its durable storage terminal succeeded.
    pub fn install_assistant_configuration(&mut self, configuration: AssistantConfiguration) {
        self.assistant_configuration.install(configuration);
    }
}
