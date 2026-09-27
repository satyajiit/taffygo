// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one published `CoreStatus`: projected, contributed to, and encoded.

use crate::ports::StatusContributionFacts;
use crate::runtime::CoreStatusProjectionGap;

use super::{recovery, ProfileServiceRuntime};

/// One bounded generated Core API payload ready for Core Service transport.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EncodedCoreStatus {
    pub schema_version: u32,
    pub payload: Vec<u8>,
    pub task_revisions: Vec<crate::runtime::TaskRevisionBindingFacts>,
    pub pending_approvals: Vec<crate::runtime::PendingApprovalBindingFacts>,
    pub pending_permissions: Vec<crate::runtime::PendingPermissionBindingFacts>,
    pub task_settlements: Vec<crate::runtime::TaskSettlementBindingFacts>,
    pub terminal_tasks: Vec<crate::runtime::TerminalTaskBindingFacts>,
    pub accepted_task_consents: Vec<crate::runtime::AcceptedTaskConsentBindingFacts>,
    pub committed_action_approvals: Vec<crate::runtime::CommittedActionApprovalBindingFacts>,
}

/// A canonical runtime fact or generated codec prevented truthful publication.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CoreStatusEncodingError {
    Projection(CoreStatusProjectionGap),
    Codec(core_api_types::CoreStatusPayloadCodecError),
}

impl ProfileServiceRuntime {
    /// Projects the immutable Core API state without inventing absent facts.
    pub fn project_core_status(
        &self,
        availability: core_api_types::CoreAvailability,
    ) -> Result<core_api_types::CoreStatus, CoreStatusProjectionGap> {
        let mut status = self.core.project_core_status(availability)?;
        self.contribute_status(&mut status);
        Ok(status)
    }

    /// Encodes the complete immutable ready-state projection for transport.
    pub fn encode_ready_core_status(&self) -> Result<EncodedCoreStatus, CoreStatusEncodingError> {
        let mut projection = self
            .core
            .project_core_status_with_approvals(core_api_types::CoreAvailability::Ready)
            .map_err(CoreStatusEncodingError::Projection)?;
        self.contribute_status(&mut projection.status);
        projection.committed_action_approvals.retain(|approval| {
            approval.browser_session_id == self.browser_session_id.as_str()
                && approval.expires_at_utc_ms > self.clock.utc_millis()
        });
        let payload = recovery::encode(
            &mut projection.status,
            self.providers.status_projection().revision(),
        )
        .map_err(CoreStatusEncodingError::Codec)?;
        Ok(EncodedCoreStatus {
            schema_version: core_api_types::CORE_STATUS_PAYLOAD_SCHEMA_VERSION,
            payload,
            task_revisions: projection.task_revisions,
            pending_approvals: projection.pending_approvals,
            pending_permissions: projection.pending_permissions,
            task_settlements: projection.task_settlements,
            terminal_tasks: projection.terminal_tasks,
            accepted_task_consents: projection.accepted_task_consents,
            committed_action_approvals: projection.committed_action_approvals,
        })
    }

    /// Walks the contributors in order over one projection (decision 0073).
    pub(super) fn contribute_status(&self, status: &mut core_api_types::CoreStatus) {
        let ask_prompts = self.core.staged_ask_prompts();
        let entitlement = self.entitlement_refresh.display();
        let provider_probes = self.probes.display();
        let assistant_configuration = self.assistant_configuration.view();
        let asset_installations = self.core.asset_installations();
        let builtin_skills = self
            .builtin_skills
            .status_views(&self.assistant_configuration, &asset_installations);
        let facts = StatusContributionFacts {
            builtin_skills: &builtin_skills,
            available_account_methods: &self.available_account_methods,
            ask_prompts: &ask_prompts,
            provider_status: self.providers.status_projection(),
            entitlement: entitlement.as_ref(),
            provider_probes: &provider_probes,
        };
        for contributor in &self.contributors {
            contributor.contribute(&facts, status);
        }
        status.assistant_configuration = assistant_configuration;
        status.saved_sign_ins = self.saved_data.sign_ins();
        status.saved_details = self.saved_data.details();
        status.site_skills = self.procedures.site_skill_views();
    }
}
