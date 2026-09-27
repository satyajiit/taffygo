// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Construction of the one canonical profile runtime.
//!
//! The browser-minted facts a generation is built from, the closed set of
//! reasons construction refuses, and the single function that assembles every
//! reviewed production domain. Split out of `profile.rs` unchanged; the names
//! are re-exported there, so every caller and every public path is the same.

use std::rc::Rc;

use crate::account::{AccountAuthMethod, Sha256Port};
use crate::adapters::account::{AccountOperationRuntime, ProductionAccount};
use crate::adapters::assets::{GenerationJitter, ProductionAssetDelivery};
use crate::adapters::audit::ProductionAudit;
use crate::adapters::crosscutting::{
    ProductionAccountMethods, ProductionAskPrompts, ProductionEntitlementView,
    ProductionProviderModels, ProductionProviderProbes, ProductionProviderRoster,
    ProductionTurnObserver,
};
use crate::adapters::ids::GenerationEntropy;
use crate::adapters::library::ProductionLibrary;
use crate::adapters::memory::ProductionMemory;
use crate::adapters::model::ModelRouterBuildError;
use crate::adapters::persistence::ProductionStorage;
use crate::adapters::policy::ProductionPolicy;
use crate::adapters::task::ProductionTaskFactory;
use crate::adapters::time::ServiceClock;
use crate::adapters::workspace::ProductionWorkspaces;
use crate::assistant_configuration::{AssistantConfiguration, AssistantConfigurationError};
use crate::builtin_skills::{
    BuiltinSkillCatalogue, BuiltinSkillCatalogueError, ProductionBuiltinSkills,
};
use crate::contract::ServiceGeneration;
use crate::entitlement_refresh::EntitlementRefreshProtocol;
use crate::procedure_catalogue::{ProcedureCatalogue, ProcedureCatalogueError};
use crate::provider::ProviderProtocol;
use crate::provider_listing::ProviderListingProtocol;
use crate::runtime::{create_service_runtime, ServiceRuntimeComponents};

use super::super::provider_catalog::{catalog_models, catalog_providers};
use super::{catalog, ProfileServiceRuntime};

/// Why the canonical production component set could not be constructed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProfileRuntimeBuildError {
    InvalidGeneration,
    InvalidGenerationEntropy,
    InvalidStorageCodec,
    InvalidModelCatalog(ModelRouterBuildError),
    InvalidProfileIdentity,
    InvalidAccountMethods,
    InvalidProcedureCatalogue(ProcedureCatalogueError),
    InvalidAssistantConfiguration(AssistantConfigurationError),
    InvalidBuiltinSkills(BuiltinSkillCatalogueError),
    /// The compiled catalog files a provider under an identity the provider
    /// plane — and therefore the platform secure store — cannot hold.
    InvalidCatalogProvider,
}

/// Browser-minted facts required to construct one profile generation.
///
/// The capability entropy is consumed during construction and is never
/// retained as ambient configuration.
pub struct ProfileRuntimeConfiguration {
    pub generation: ServiceGeneration,
    pub generation_capability_entropy: [u8; 32],
    pub initial_utc_millis: u64,
    pub private_profile: bool,
    pub browser_profile_id: String,
    pub browser_session_id: String,
    pub available_account_methods: Vec<AccountAuthMethod>,
    /// Installed definitions and content-free recent runs restored atomically.
    pub skills: Vec<core_service_types::SkillRecord>,
    pub recall: Vec<core_service_types::SkillRunRecord>,
    /// Durable abilities and response-style preferences for the one assistant.
    pub assistant_configuration: Option<core_service_types::AssistantConfiguration>,
}

/// Builds all reviewed production domains inside Rust.
///
/// `digest` is the service adapter for Chromium/BoringSSL SHA-256. Entropy and
/// time are browser-minted facts; no component reads ambient randomness or a
/// system clock.
#[allow(clippy::too_many_lines)]
pub fn create_profile_service_runtime(
    configuration: ProfileRuntimeConfiguration,
    digest: Rc<dyn Sha256Port>,
) -> Result<ProfileServiceRuntime, ProfileRuntimeBuildError> {
    let ProfileRuntimeConfiguration {
        generation,
        generation_capability_entropy,
        initial_utc_millis,
        private_profile,
        browser_profile_id,
        browser_session_id,
        available_account_methods,
        skills,
        recall,
        assistant_configuration,
    } = configuration;
    if generation.value() == 0 {
        return Err(ProfileRuntimeBuildError::InvalidGeneration);
    }
    let generation_entropy = GenerationEntropy::new(generation_capability_entropy)
        .map_err(|_| ProfileRuntimeBuildError::InvalidGenerationEntropy)?;
    if browser_profile_id.is_empty()
        || browser_profile_id.len() > core_service_types::MAX_IDENTIFIER_BYTES
    {
        return Err(ProfileRuntimeBuildError::InvalidProfileIdentity);
    }
    let browser_session_id = task_engine::BrowserSessionId::new(browser_session_id)
        .map_err(|_| ProfileRuntimeBuildError::InvalidProfileIdentity)?;
    if available_account_methods.len() > core_service_types::MAX_ACCOUNT_LINKED_PROVIDERS
        || available_account_methods
            .iter()
            .enumerate()
            .any(|(index, method)| {
                available_account_methods
                    .iter()
                    .skip(index.saturating_add(1))
                    .any(|candidate| candidate == method)
            })
    {
        return Err(ProfileRuntimeBuildError::InvalidAccountMethods);
    }
    let clock = ServiceClock::at(initial_utc_millis);
    // Bootstrap has no task milestone. Validate the stored language against
    // the complete product vocabulary here; task admission revalidates the
    // selected definition against that task's actual milestone below.
    let procedures = ProcedureCatalogue::restore(skills, recall, task_engine::Milestone::M8)
        .map_err(ProfileRuntimeBuildError::InvalidProcedureCatalogue)?;
    let assistant_configuration = AssistantConfiguration::restore(assistant_configuration)
        .map_err(ProfileRuntimeBuildError::InvalidAssistantConfiguration)?;
    let builtin_skills = BuiltinSkillCatalogue::production()
        .map_err(ProfileRuntimeBuildError::InvalidBuiltinSkills)?;
    // The router is built before the component set so the provider plane can be
    // built over the same catalog rather than over a second list of vendors.
    let (models, catalog_baseline) = catalog::bootstrap_catalog()?;
    let provider_rows =
        catalog_providers(&models).map_err(|_| ProfileRuntimeBuildError::InvalidCatalogProvider)?;
    let provider_models =
        catalog_models(&models).map_err(|_| ProfileRuntimeBuildError::InvalidCatalogProvider)?;
    let providers = ProviderProtocol::new(provider_rows, provider_models);
    let components = ServiceRuntimeComponents {
        digest: digest.clone(),
        task_factory: Box::new(ProductionTaskFactory::new(clock.clone(), digest.clone())),
        policy: Box::new(ProductionPolicy::new(
            &generation_entropy,
            generation,
            digest.clone(),
        )),
        audit: Box::new(ProductionAudit::new(digest.clone())),
        models: Box::new(models),
        storage: Box::new(
            ProductionStorage::new().map_err(|_| ProfileRuntimeBuildError::InvalidStorageCodec)?,
        ),
        account: Box::new(ProductionAccount::with_available_methods(
            available_account_methods.clone(),
        )),
        workspaces: Box::new(ProductionWorkspaces::new()),
        library: Box::new(ProductionLibrary::new(private_profile)),
        memory: Box::new(ProductionMemory::new(private_profile)),
        assets: Box::new(ProductionAssetDelivery::new(Box::new(
            GenerationJitter::new(generation_capability_entropy),
        ))),
        observers: vec![Box::new(ProductionTurnObserver::default())],
    };
    // Built before the struct literal because it borrows the session identity
    // that the literal moves, and bound to both browser-minted facts because
    // neither alone is enough: the generation restarts at one with every
    // browser launch, and the session identity is the same for every service
    // incarnation within one launch (decision 0099).
    let probes = crate::probe::ProviderProbeProtocol::new(&browser_session_id, generation);
    let completions = crate::completion::CompletionProtocol::new(&browser_session_id, generation);
    let backup_restore = crate::backup_restore_protocol::BackupRestoreProtocol::new(
        browser_profile_id.clone(),
        generation.value(),
        private_profile,
    );
    let saved_data = if private_profile {
        crate::saved_data::SavedDataState::unavailable()
    } else {
        crate::saved_data::SavedDataState::loading()
    };
    Ok(ProfileServiceRuntime {
        core: create_service_runtime(generation, components),
        account_operations: AccountOperationRuntime::new(),
        clock,
        digest,
        backup_restore,
        private_profile,
        browser_profile_id,
        browser_session_id,
        available_account_methods,
        providers,
        catalog_baseline,
        listings: ProviderListingProtocol::default(),
        entitlement_refresh: EntitlementRefreshProtocol::default(),
        probes,
        completions,
        procedures,
        assistant_configuration,
        builtin_skills,
        saved_data,
        contributors: vec![
            Box::new(ProductionAccountMethods),
            Box::new(ProductionAskPrompts),
            Box::new(ProductionProviderRoster),
            Box::new(ProductionProviderModels),
            Box::new(ProductionEntitlementView),
            Box::new(ProductionProviderProbes),
            Box::new(ProductionBuiltinSkills),
        ],
        last_model_reading: "none",
        last_refusals_on_sight: Vec::new(),
    })
}
