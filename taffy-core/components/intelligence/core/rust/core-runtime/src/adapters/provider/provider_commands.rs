// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One typed provider command, applied to the plane that owns the state.
//!
//! Every write in this module goes through `&mut ProviderProtocol` on the one
//! ordered core sequence, which is what OD-103 asks for and what the borrow
//! checker enforces here without a lock anybody has to remember to take. The
//! authoritative check is inside the plane's own method, on the state as it is
//! at that moment, never on a copy read earlier by this file.

use model_router::{CredentialState, ThinkingLevel};

use crate::provider::{
    CredentialHandle, ProviderAuthMethod, ProviderEffect, ProviderError, ProviderId,
    ProviderProtocol, ProviderWireApi,
};

mod custom_provider;

/// Why one provider command was refused.
///
/// Split from [`ProviderError`] because these are refusals about the *command*
/// — a body that does not match its kind, a kind this seam does not serve —
/// while every `ProviderError` is a refusal the plane reached on its own state.
/// Flattening them would make "the caller sent nonsense" and "the provider does
/// not offer that method" the same answer.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProviderServiceError {
    /// The command's kind is not one this seam serves.
    UnsupportedKind,
    /// The kind named a body the command did not carry.
    MissingBody,
    /// The command named the managed wire family, which is spoken only to the
    /// product's own service at its compiled origin — a person's provider can
    /// never speak it, so a command claiming one does is malformed.
    ReservedWireApi,
    /// The plane refused on its own facts.
    Domain(ProviderError),
}

/// What one accepted provider command did.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProviderServiceStep {
    /// Whether the published state must be taken again.
    ///
    /// Always true for an accepted write here, and named rather than assumed
    /// because the caller's contract is "publish when this is set" — the same
    /// shape the account plane's step carries.
    pub state_changed: bool,
    /// A secure-store handle the core no longer references.
    ///
    /// The browser owns the material behind it. Nothing in this repository
    /// deletes it yet, and that is deliberate rather than forgotten: the
    /// Android provider store is keyed by provider id
    /// (`AndroidProfileSecureMaterialStore.storeProviderCredential` replaces a
    /// record in place, and `removeProviderCredential` takes the id alone), so
    /// the surface that asked for this change can already drop its own record
    /// and no handle is orphaned by a replacement. It is carried here rather
    /// than discarded so that a store which ever stops being id-keyed has one
    /// named value to route, instead of a fact the core computed and threw
    /// away.
    pub released_handle: Option<CredentialHandle>,
}

impl ProviderServiceStep {
    fn from_effect(effect: &ProviderEffect) -> Self {
        Self {
            state_changed: true,
            released_handle: effect.released_handle().cloned(),
        }
    }
}

/// Applies one generated provider command to the plane.
///
/// Validation is complete before any state is touched: each identity, name,
/// endpoint and handle is parsed into its bounded type first, and only a
/// command whose every field parsed reaches a `&mut` method.
pub fn apply_provider_command(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    match command.kind {
        core_service_types::CoreServiceCommandKind::SaveProviderCredential => {
            save_credential(plane, command)
        }
        core_service_types::CoreServiceCommandKind::ForgetProviderCredential => {
            forget_credential(plane, command)
        }
        core_service_types::CoreServiceCommandKind::StartProviderAuth => {
            start_provider_auth(plane, command)
        }
        core_service_types::CoreServiceCommandKind::ProviderAuthCallback => {
            provider_auth_callback(plane, command)
        }
        core_service_types::CoreServiceCommandKind::CancelProviderAuth => {
            cancel_provider_auth(plane, command)
        }
        core_service_types::CoreServiceCommandKind::SaveCustomProvider => {
            custom_provider::save_custom_provider(plane, command)
        }
        core_service_types::CoreServiceCommandKind::RemoveCustomProvider => {
            remove_custom_provider(plane, command)
        }
        core_service_types::CoreServiceCommandKind::SetProviderCredentialState => {
            set_credential_state(plane, command)
        }
        core_service_types::CoreServiceCommandKind::SetProviderModelPreference => {
            set_model_preference(plane, command)
        }
        // Neither probe is a registry write, and neither is a composer
        // suggestion or its withdrawal: each is composed or withdrawn by the
        // profile composition (decisions 0083, 0097 and 0098), so the
        // record-keeping plane refuses all four like any other foreign kind.
        // The custom-endpoint probe is the one worth naming, because it looks
        // like a registry write and is not: it names the identity a save will
        // later be filed under, and nothing is filed until that save arrives.
        core_service_types::CoreServiceCommandKind::ProbeProviderCredential
        | core_service_types::CoreServiceCommandKind::ProbeCustomEndpoint
        | core_service_types::CoreServiceCommandKind::RequestComposerCompletion
        | core_service_types::CoreServiceCommandKind::CancelComposerCompletion
        | core_service_types::CoreServiceCommandKind::PauseTask
        | core_service_types::CoreServiceCommandKind::ResumeTask
        | core_service_types::CoreServiceCommandKind::TakeOver
        | core_service_types::CoreServiceCommandKind::SetAssistantConfiguration
        | core_service_types::CoreServiceCommandKind::StartTask
        | core_service_types::CoreServiceCommandKind::CancelTask
        | core_service_types::CoreServiceCommandKind::UserDecision
        | core_service_types::CoreServiceCommandKind::AuthCallback
        | core_service_types::CoreServiceCommandKind::PermissionResult
        | core_service_types::CoreServiceCommandKind::StartAuth
        | core_service_types::CoreServiceCommandKind::RequestEmailLink
        | core_service_types::CoreServiceCommandKind::SignOut
        | core_service_types::CoreServiceCommandKind::AuthCredentialResult
        | core_service_types::CoreServiceCommandKind::CorrectWorkspaceFact
        | core_service_types::CoreServiceCommandKind::ExcludeWorkspaceSource
        | core_service_types::CoreServiceCommandKind::RequestWorkspaceExport
        | core_service_types::CoreServiceCommandKind::SaveWorkspace
        | core_service_types::CoreServiceCommandKind::RenameWorkspace
        | core_service_types::CoreServiceCommandKind::DeleteWorkspace
        | core_service_types::CoreServiceCommandKind::DiscardWorkspace
        | core_service_types::CoreServiceCommandKind::SearchLibrary
        | core_service_types::CoreServiceCommandKind::SaveLibraryFact
        | core_service_types::CoreServiceCommandKind::RemoveLibraryEntry
        | core_service_types::CoreServiceCommandKind::RequestLibraryExport
        | core_service_types::CoreServiceCommandKind::SearchMemory
        | core_service_types::CoreServiceCommandKind::UpsertMemory
        | core_service_types::CoreServiceCommandKind::DeleteMemory
        | core_service_types::CoreServiceCommandKind::AcceptTaskArtifact
        | core_service_types::CoreServiceCommandKind::ExportTaskArtifact
        | core_service_types::CoreServiceCommandKind::SetAssetDeliveryPolicy
        | core_service_types::CoreServiceCommandKind::RequestAsset
        | core_service_types::CoreServiceCommandKind::RemoveAsset
        | core_service_types::CoreServiceCommandKind::CompleteHandover
        | core_service_types::CoreServiceCommandKind::ExpireHandover
        | core_service_types::CoreServiceCommandKind::SupplyUserInput
        | core_service_types::CoreServiceCommandKind::FollowUp
        | core_service_types::CoreServiceCommandKind::SupplyFieldValues
        | core_service_types::CoreServiceCommandKind::ReplaceSavedDataSnapshot
        | core_service_types::CoreServiceCommandKind::MutateSkill => {
            Err(ProviderServiceError::UnsupportedKind)
        }
    }
}

fn save_credential(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .save_provider_credential
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    let provider_id = ProviderId::new(body.provider_id.as_str()).map_err(domain)?;
    let handle = CredentialHandle::new(body.credential_handle.as_str())
        .map_err(|error| domain(error.into()))?;
    let effect = plane
        .save_credential(provider_id, auth_method(body.auth_method), handle)
        .map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

fn forget_credential(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .forget_provider_credential
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    let provider_id = ProviderId::new(body.provider_id.as_str()).map_err(domain)?;
    let effect = plane.forget_credential(&provider_id).map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

fn set_credential_state(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .set_provider_credential_state
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    let provider_id = ProviderId::new(body.provider_id.as_str()).map_err(domain)?;
    let effect = plane
        .set_credential_state(&provider_id, credential_state(body.state))
        .map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

/// Files the standing model choice one surface stated (decision 0093).
///
/// Both fields carry the choice as it should now stand, so nothing here reads
/// what is held first: an absent model is a cleared pin rather than a field
/// left alone, and an absent rung is "Taffy decides" rather than the rung from
/// before. The model identity is handed over as it arrived and resolved
/// against the catalog inside the plane, so what is stored is the catalog's
/// own spelling and an identity no provider carries is refused rather than
/// parsed into a type and then found missing.
fn set_model_preference(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .set_provider_model_preference
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    let provider_id = ProviderId::new(body.provider_id.as_str()).map_err(domain)?;
    let effect = plane
        .set_model_preference(
            &provider_id,
            body.model_id.as_deref(),
            body.thinking
                .as_ref()
                .map(|preference| thinking_level(preference.level)),
        )
        .map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

fn remove_custom_provider(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .remove_custom_provider
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    let provider_id = ProviderId::new(body.provider_id.as_str()).map_err(domain)?;
    let effect = plane.remove_custom_provider(&provider_id).map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

/// Admits one subscription sign-in and records it as pending.
///
/// Admission is the whole of the core's part (decision 0078): the browser
/// mints the flow identity, runs every network leg, owns the deadline and the
/// redirect-state comparison, and reports the terminal through
/// [`provider_auth_callback`]. The plane decides on its own facts — the
/// provider offers OAUTH, ships switched on, and has no flow already pending —
/// so a flow can never start against a provider the eventual save would
/// refuse.
fn start_provider_auth(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .start_provider_auth
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    let provider_id = ProviderId::new(body.provider_id.as_str()).map_err(domain)?;
    if !bounded_identifier(&body.flow_id) || !bounded_identifier(&body.redirect_binding_id) {
        return Err(ProviderServiceError::MissingBody);
    }
    let effect = plane
        .begin_sign_in(
            provider_id,
            body.flow_id.clone(),
            body.redirect_binding_id.clone(),
        )
        .map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

/// Files a sign-in flow's terminal and clears its pending record.
///
/// The redirect itself was correlated in the browser: the state comparison
/// and the deadline are the sign-in engine's (decision 0078), and a redirect
/// that fails either never reaches this seam. What the plane checks is what
/// the plane recorded — the flow identity and its binding — plus the one
/// structural fact the contract closes: a terminal that claims a code must
/// carry its handle, and one that does not must not.
fn provider_auth_callback(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .provider_auth_callback
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    if !bounded_identifier(&body.flow_id)
        || !bounded_identifier(&body.redirect_binding_id)
        || body.returned_state.len() > core_service_types::MAX_IDENTIFIER_BYTES
    {
        return Err(ProviderServiceError::MissingBody);
    }
    if !body.has_valid_presence() {
        return Err(domain(ProviderError::MalformedRedirect));
    }
    if let Some(handle) = body.authorization_code_handle.as_ref() {
        if !bounded_identifier(handle) {
            return Err(ProviderServiceError::MissingBody);
        }
    }
    let effect = plane
        .finish_sign_in(&body.flow_id, &body.redirect_binding_id)
        .map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

/// Removes the exact pending flow before the browser cancels native work.
fn cancel_provider_auth(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .cancel_provider_auth
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    if !bounded_identifier(&body.flow_id) {
        return Err(ProviderServiceError::MissingBody);
    }
    let effect = plane.cancel_sign_in(&body.flow_id).map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

/// Whether a browser-minted identifier is present and within the wire bound.
fn bounded_identifier(value: &str) -> bool {
    !value.is_empty() && value.len() <= core_service_types::MAX_IDENTIFIER_BYTES
}

const fn domain(error: ProviderError) -> ProviderServiceError {
    ProviderServiceError::Domain(error)
}

const fn credential_state(state: core_service_types::ProviderCredentialState) -> CredentialState {
    match state {
        core_service_types::ProviderCredentialState::Usable => CredentialState::Usable,
        core_service_types::ProviderCredentialState::NeedsSignIn => CredentialState::NeedsSignIn,
        core_service_types::ProviderCredentialState::RefreshFailed => {
            CredentialState::RefreshFailed
        }
    }
}

/// The plane's spelling of one rung, member for member with the contract's.
const fn thinking_level(level: core_service_types::ThinkingLevel) -> ThinkingLevel {
    match level {
        core_service_types::ThinkingLevel::Off => ThinkingLevel::Off,
        core_service_types::ThinkingLevel::Minimal => ThinkingLevel::Minimal,
        core_service_types::ThinkingLevel::Low => ThinkingLevel::Low,
        core_service_types::ThinkingLevel::Medium => ThinkingLevel::Medium,
        core_service_types::ThinkingLevel::High => ThinkingLevel::High,
        core_service_types::ThinkingLevel::Xhigh => ThinkingLevel::XHigh,
        core_service_types::ThinkingLevel::Max => ThinkingLevel::Max,
    }
}

const fn auth_method(method: core_service_types::ProviderAuthMethod) -> ProviderAuthMethod {
    match method {
        core_service_types::ProviderAuthMethod::ApiKey => ProviderAuthMethod::ApiKey,
        core_service_types::ProviderAuthMethod::Oauth => ProviderAuthMethod::Oauth,
    }
}

const fn wire_api(
    api: core_service_types::ProviderWireApi,
) -> Result<ProviderWireApi, ProviderServiceError> {
    match api {
        core_service_types::ProviderWireApi::AnthropicMessages => {
            Ok(ProviderWireApi::AnthropicMessages)
        }
        core_service_types::ProviderWireApi::OpenAiResponses => {
            Ok(ProviderWireApi::OpenAiResponses)
        }
        core_service_types::ProviderWireApi::OpenAiCompletions => {
            Ok(ProviderWireApi::OpenAiCompletions)
        }
        core_service_types::ProviderWireApi::GoogleGenerativeLanguage => {
            Ok(ProviderWireApi::GoogleGenerativeLanguage)
        }
        // The plane's own enumeration holds none of these on purpose. The
        // managed wire is the product's own envelope, and the other two are
        // vendors' subscription endpoints reached with a subscription
        // credential — a person's own endpoint is neither, so a custom
        // provider naming any of them is refused rather than saved as a
        // provider nothing would route to.
        core_service_types::ProviderWireApi::Managed
        | core_service_types::ProviderWireApi::OpenAiCodexResponses
        | core_service_types::ProviderWireApi::GoogleCloudCodeAssist => {
            Err(ProviderServiceError::ReservedWireApi)
        }
    }
}

#[cfg(test)]
mod tests;
