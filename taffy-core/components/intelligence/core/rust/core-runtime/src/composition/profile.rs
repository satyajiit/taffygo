// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One canonical production composition for a profile utility service.

use std::rc::Rc;

use bip_types::identity::TaskId;
use task_engine::CommandEnvelope;

use crate::account::{AccountAuthMethod, Sha256Port};
use crate::adapters::account::AccountOperationRuntime;
use crate::adapters::time::ServiceClock;
use crate::assistant_configuration::AssistantConfiguration;
use crate::builtin_skills::BuiltinSkillCatalogue;
use crate::codec::policy_wire::evaluate_policy_request;
use crate::contract::{CancellationReason, OperationId};
use crate::entitlement_refresh::EntitlementRefreshProtocol;
use crate::ports::StatusContributorPort;
use crate::procedure_catalogue::ProcedureCatalogue;
use crate::provider::ProviderProtocol;
use crate::provider_listing::ProviderListingProtocol;
use crate::runtime::{BoxedCoreRuntime, TaskCompletionError, TaskTerminal};
use loop_kernel::walk::ReviewedWorkflowError;

mod account;
mod assistant;
mod backup;
mod build;
mod catalog;
pub mod completion;
mod debug;
pub mod endpoint_probe;
mod entitlement;
mod listing;
mod model;
pub mod probe;
mod procedure;
mod provider;
mod recording;
mod recovery;
mod restore;
mod saved_flows;
mod status;

pub use build::{
    create_profile_service_runtime, ProfileRuntimeBuildError, ProfileRuntimeConfiguration,
};
pub use model::{ModelAttemptDispatch, ModelCompletionOutcome, ModelFailureOutcome, PlannedTurn};
pub use status::{CoreStatusEncodingError, EncodedCoreStatus};

/// Canonical profile-owned service runtime and its injected deterministic sources.
pub struct ProfileServiceRuntime {
    core: BoxedCoreRuntime,
    account_operations: AccountOperationRuntime,
    clock: ServiceClock,
    digest: Rc<dyn Sha256Port>,
    backup_restore: crate::backup_restore_protocol::BackupRestoreProtocol,
    private_profile: bool,
    browser_profile_id: String,
    browser_session_id: task_engine::BrowserSessionId,
    available_account_methods: Vec<AccountAuthMethod>,
    providers: ProviderProtocol,
    /// The baseline document the merge always starts from.
    catalog_baseline: model_router::catalog::types::CatalogDocument,
    /// Every connected provider that serves its own model list (decision 0098).
    listings: ProviderListingProtocol,
    entitlement_refresh: EntitlementRefreshProtocol,
    probes: crate::probe::ProviderProbeProtocol,
    completions: crate::completion::CompletionProtocol,
    procedures: ProcedureCatalogue,
    assistant_configuration: AssistantConfiguration,
    builtin_skills: BuiltinSkillCatalogue,
    saved_data: crate::saved_data::SavedDataState,
    /// Status contributors, in order (decision 0073). Built once, here only.
    contributors: Vec<Box<dyn StatusContributorPort>>,
    /// See [`Self::last_model_reading`].
    last_model_reading: &'static str,
    /// See [`Self::last_refusals_on_sight`].
    last_refusals_on_sight: Vec<&'static str>,
}

impl ProfileServiceRuntime {
    /// Whether this profile is off-the-record and therefore account-free.
    pub const fn private_profile(&self) -> bool {
        self.private_profile
    }

    /// Exact browser-owned profile partition identity.
    pub fn browser_profile_id(&self) -> &str {
        &self.browser_profile_id
    }

    /// Browser-session identity used to reject stale `TimeTicks` authority.
    pub const fn browser_session_id(&self) -> &task_engine::BrowserSessionId {
        &self.browser_session_id
    }

    /// Updates reducer audit time from the browser before one ordered call.
    pub fn set_utc_millis(&self, now_utc_millis: u64) {
        self.clock.set_utc_millis(now_utc_millis);
    }

    /// Latest browser-minted UTC fact installed on this ordered runtime.
    pub fn utc_millis(&self) -> u64 {
        self.clock.utc_millis()
    }

    /// Replaces the resident UI projection from one trusted Chromium store read.
    pub fn replace_saved_data_snapshot(
        &mut self,
        snapshot: core_service_types::ReplaceSavedDataSnapshotCommand,
    ) -> Result<(), crate::saved_data::SavedDataSnapshotError> {
        self.saved_data.replace(snapshot, self.private_profile)
    }

    /// The closed runtime surface used by the generated service adapter.
    pub const fn core(&self) -> &BoxedCoreRuntime {
        &self.core
    }

    /// Mutable access to the closed runtime surface on its one ordered sequence.
    pub const fn core_mut(&mut self) -> &mut BoxedCoreRuntime {
        &mut self.core
    }

    /// Computes the next replay-stable command for the reviewed local workflow.
    ///
    /// The outer `None` means the task identity is not open in this profile;
    /// the inner `None` means the reducer is truthfully waiting or terminal.
    pub fn next_reviewed_task_command(
        &self,
        task_id: &TaskId,
    ) -> Option<Result<Option<CommandEnvelope>, ReviewedWorkflowError>> {
        self.core.task(task_id).map(|task| {
            loop_kernel::walk::next_reviewed_command_envelope(task, self.digest.as_ref())
        })
    }

    /// Cancels one operation owned by this profile and returns its task terminal.
    pub fn cancel_operation(
        &mut self,
        operation_id: &OperationId,
        reason: CancellationReason,
    ) -> Result<TaskTerminal, TaskCompletionError> {
        self.core.cancel_operation(operation_id, reason)
    }

    /// Evaluates one complete generated browser fact set without glue defaults.
    pub fn evaluate_policy(
        &mut self,
        request: &core_service_types::PolicyEvaluationRequest,
    ) -> core_service_types::PolicyEvaluationResult {
        evaluate_policy_request(&mut self.core, request)
    }

    /// The same evaluation, with the name of the clause that refused it.
    pub fn evaluate_policy_labeled(
        &mut self,
        request: &core_service_types::PolicyEvaluationRequest,
    ) -> (core_service_types::PolicyEvaluationResult, &'static str) {
        crate::codec::policy_wire::evaluate_policy_request_labeled(&mut self.core, request)
    }
}

#[cfg(test)]
mod tests;
