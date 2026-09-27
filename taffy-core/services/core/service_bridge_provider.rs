// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Provider-plane commands across the CXX boundary.
//!
//! Ten commands arrive — save a key, forget one, report a stored
//! credential's registry state, start or cancel a subscription sign-in,
//! deliver its terminal, define one of a person's own providers, remove one,
//! prove a key, and prove an address a person typed — and every accepted one
//! ends in [`response_after_change`]. That is not bookkeeping. A surface reads
//! `CoreStatus`, and `CoreStatus` reaches it only when a bridge publishes a
//! state; the delivery bridge published none and a pack could download, verify
//! and install while every screen went on showing the snapshot taken at
//! bootstrap. A provider write that published nothing would be the same defect
//! with the same symptom: a person saves a key, the core has it, routing uses
//! it, and no screen ever says so.
//!
//! An accepted write also carries away whatever listing fetch it made possible
//! (decision 0098). That is planned here rather than by a poke of its own
//! because a listing is per-account: it becomes askable at the moment a
//! credential for that provider goes on file, and that moment is visible from
//! this seam and from nowhere else. `service_bridge_listing.rs` owns what gets
//! planned and what comes back.

use core_runtime::provider::ProviderError;
use core_runtime::wire;
use core_runtime::ProviderServiceError;

use crate::ffi;
use crate::service_bridge_provider_ffi::ffi as provider_ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

#[allow(non_snake_case)]
pub(crate) fn SubmitProvider(
    bridge: &mut ServiceBridge,
    command: provider_ffi::BridgeProviderCommand,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    if let Err(status) = validate_operation(
        &command.operation,
        bridge.generation.value(),
        now_monotonic_ms,
    ) {
        return response(&operation_id, status, Vec::new());
    }
    let Some(command) = command_to_wire(command) else {
        return response(&operation_id, invalid(), Vec::new());
    };
    if command.kind == wire::CoreServiceCommandKind::ProbeCustomEndpoint {
        // Not a registry write either: nothing is saved under an address until
        // the person saves it (decision 0096). It claims the same single
        // flight the key probe claims — one person, one sheet — so it is
        // admitted beside it and refused on the same terms, and its verdict
        // returns later through `DeliverEndpointProbeResult`. It is taken
        // before the runtime is borrowed rather than beside the key probe
        // below, because the plane that owns it borrows the bridge itself in
        // order to publish.
        return crate::service_bridge_endpoint_probe::submit(
            bridge,
            &command,
            &operation_id,
            now_utc_millis,
        );
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(&operation_id, unavailable(), Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);
    if command.kind == wire::CoreServiceCommandKind::ProbeProviderCredential {
        // The probe is not a registry write: it composes one bounded model
        // effect (decision 0083), which travels on the response so the
        // browser dispatches it, and the verdict returns later through
        // DeliverProbeCompletion.
        return match runtime.submit_probe_command(&command, now_monotonic_ms) {
            Ok(Some(effect)) => {
                let probe_effects =
                    vec![crate::service_bridge_probe::probe_effect_to_bridge(effect)];
                let mut accepted = response(
                    &operation_id,
                    wire::AdmissionStatus::Accepted as u8,
                    Vec::new(),
                );
                accepted.probe_effects = probe_effects;
                // The claimed flight is state, and publication is what makes
                // an accepted command's state visible — the same rule every
                // other accepted provider command follows.
                response_after_change(bridge, accepted)
            }
            // Nothing to send: the provider lists no model to probe with, and
            // the core filed that as the verdict NO_MODEL_LISTED rather than
            // refusing the command (Core API 3.40). Accepted with no effect,
            // and published for the same reason as the flight above — the
            // verdict is the row a sheet draws, and an unpublished one is
            // invisible rather than wrong. This used to fall through
            // `probe_error_status` as `InvalidCommand`, which a surface says
            // as "could not run right now" about a state no retry changes.
            Ok(None) => response_after_change(
                bridge,
                response(
                    &operation_id,
                    wire::AdmissionStatus::Accepted as u8,
                    Vec::new(),
                ),
            ),
            Err(error) => response(&operation_id, probe_error_status(error), Vec::new()),
        };
    }
    match runtime.submit_provider_command(&command) {
        // Every accepted provider write changes state, and the state it
        // changes is what routing reads. Publishing is therefore the last step
        // of the command rather than a courtesy to whoever is watching.
        Ok(step) => {
            debug_assert!(
                step.state_changed,
                "an accepted provider write changes state"
            );
            let mut accepted = response(
                &operation_id,
                wire::AdmissionStatus::Accepted as u8,
                Vec::new(),
            );
            // A provider write is the one moment a listing can become
            // possible: a listing is per-account and needs a credential to ask
            // with, so before the credential is on file there is nothing to
            // ask for (decision 0098 section 1). Planning is pure over what is
            // stored, so asking again here is the one decision taken again
            // with newer facts rather than a second one.
            accepted.listing_effects =
                crate::service_bridge_listing::planned_effects(bridge, now_utc_millis);
            response_after_change(bridge, accepted)
        }
        Err(error) => response(&operation_id, error_status(error), Vec::new()),
    }
}

/// The flat CXX record, back into the generated command it stands for.
///
/// Flat rather than tagged because cxx has no sum type, so the kind names
/// which fields carry meaning. A body whose kind does not match is refused
/// whole rather than repaired: a repaired provider command would be a change
/// nobody asked for, made to a person's credentials.
fn command_to_wire(value: provider_ffi::BridgeProviderCommand) -> Option<wire::CoreServiceCommand> {
    let kind = wire::CoreServiceCommandKind::from_wire(u32::from(value.kind))?;
    let mut command = wire::CoreServiceCommand {
        operation: provider_operation_to_wire(value.operation),
        kind,
        start_task: None,
        cancel_task: None,
        user_decision: None,
        auth_callback: None,
        permission_result: None,
        start_auth: None,
        request_email_link: None,
        sign_out: None,
        auth_credential_result: None,
        correct_workspace_fact: None,
        exclude_workspace_source: None,
        request_workspace_export: None,
        set_asset_delivery_policy: None,
        request_asset: None,
        remove_asset: None,
        save_provider_credential: None,
        set_provider_credential_state: None,
        forget_provider_credential: None,
        start_provider_auth: None,
        provider_auth_callback: None,
        save_custom_provider: None,
        remove_custom_provider: None,
        complete_handover: None,
        expire_handover: None,
        supply_user_input: None,
        follow_up: None,
        supply_field_values: None,
        probe_provider_credential: None,
        set_provider_model_preference: None,
        probe_custom_endpoint: None,
        request_composer_completion: None,
        cancel_composer_completion: None,
        pause_task: None,
        resume_task: None,
        take_over: None,
        set_assistant_configuration: None,
        save_workspace: None,

        rename_workspace: None,

        delete_workspace: None,

        discard_workspace: None,
        search_library: None,
        save_library_fact: None,
        remove_library_entry: None,
        request_library_export: None,
        search_memory: None,
        upsert_memory: None,
        delete_memory: None,
        accept_task_artifact: None,
        export_task_artifact: None,
        replace_saved_data_snapshot: None,
        mutate_skill: None,
        cancel_provider_auth: None,
    };
    match kind {
        wire::CoreServiceCommandKind::SaveProviderCredential => {
            command.save_provider_credential = Some(wire::SaveProviderCredentialCommand {
                provider_id: value.provider_id,
                auth_method: wire::ProviderAuthMethod::from_wire(u32::from(value.auth_method))?,
                credential_handle: value.credential_handle,
            });
        }
        wire::CoreServiceCommandKind::ForgetProviderCredential => {
            command.forget_provider_credential = Some(wire::ForgetProviderCredentialCommand {
                provider_id: value.provider_id,
            });
        }
        wire::CoreServiceCommandKind::SetProviderCredentialState => {
            command.set_provider_credential_state =
                Some(wire::SetProviderCredentialStateCommand {
                    provider_id: value.provider_id,
                    state: wire::ProviderCredentialState::from_wire(u32::from(
                        value.credential_state,
                    ))?,
                    // Empty is the contract's own word for "the vendor said
                    // nothing about which models this credential reaches", and
                    // that is exactly what a bridge record with no such field
                    // knows. Widening the seam to carry a roster is the
                    // provider plane's own change, not this projection's.
                    available_model_ids: Vec::new(),
                });
        }
        wire::CoreServiceCommandKind::StartProviderAuth => {
            command.start_provider_auth = Some(wire::StartProviderAuthCommand {
                flow_id: value.flow_id,
                provider_id: value.provider_id,
                redirect_binding_id: value.redirect_binding_id,
                issued_at_monotonic_ms: value.issued_at_monotonic_ms,
            });
        }
        wire::CoreServiceCommandKind::CancelProviderAuth => {
            command.cancel_provider_auth = Some(wire::CancelProviderAuthCommand {
                flow_id: value.flow_id,
            });
        }
        wire::CoreServiceCommandKind::ProviderAuthCallback => {
            command.provider_auth_callback = Some(wire::ProviderAuthCallbackCommand {
                flow_id: value.flow_id,
                redirect_binding_id: value.redirect_binding_id,
                returned_state: value.returned_state,
                status: wire::AuthCallbackStatus::from_wire(u32::from(value.callback_status))?,
                authorization_code_handle: value
                    .has_authorization_code_handle
                    .then_some(value.authorization_code_handle),
            });
        }
        wire::CoreServiceCommandKind::SaveCustomProvider => {
            // Bounded here as well as inside the plane, because this is where
            // a list a surface assembled enters the core and the contract's
            // limit is the product's rather than the caller's. The count is
            // read from the contract rather than written down, so the seam and
            // the plane cannot come to hold two different numbers.
            if value.models.len() > wire::MAX_CUSTOM_MODEL_ENTRIES {
                return None;
            }
            // Absent, never a member standing for absence. The contract wraps
            // the enumeration in a record precisely so that "nothing was
            // detected" has no member of its own, and a projection that
            // invented one would be answering a question the probe did not.
            let detected_server = if value.has_detected_server {
                Some(wire::DetectedServer {
                    server_kind: wire::ServerKind::from_wire(u32::from(value.detected_server))?,
                })
            } else {
                None
            };
            command.save_custom_provider = Some(wire::SaveCustomProviderCommand {
                provider_id: value.provider_id,
                display_name: value.display_name,
                endpoint: value.endpoint,
                wire_api: wire::ProviderWireApi::from_wire(u32::from(value.wire_api))?,
                credential_handle: value
                    .has_credential_handle
                    .then_some(value.credential_handle),
                // The models the probe found, in the order the endpoint listed
                // them, on the same write that files the provider: a provider
                // saved with nothing behind it is one a person can select and
                // nothing can route to (decision 0096 section 4).
                models: value.models.into_iter().map(custom_model_to_wire).collect(),
                detected_server,
            });
        }
        wire::CoreServiceCommandKind::RemoveCustomProvider => {
            command.remove_custom_provider = Some(wire::RemoveCustomProviderCommand {
                provider_id: value.provider_id,
            });
        }
        wire::CoreServiceCommandKind::ProbeProviderCredential => {
            command.probe_provider_credential = Some(wire::ProbeProviderCredentialCommand {
                provider_id: value.provider_id,
                credential_handle: value.credential_handle,
            });
        }
        wire::CoreServiceCommandKind::ProbeCustomEndpoint => {
            command.probe_custom_endpoint = Some(wire::ProbeCustomEndpointCommand {
                endpoint: value.endpoint,
                wire_api: wire::ProviderWireApi::from_wire(u32::from(value.wire_api))?,
                // Optional here and required by the credential probe beside
                // it, because they prove different things: a key probe has a
                // key to prove, and an address a person runs themselves may
                // need none at all (decision 0096). The flag is what keeps an
                // absent handle apart from an empty one across a bridge with
                // no optional of its own.
                credential_handle: value
                    .has_credential_handle
                    .then_some(value.credential_handle),
                // The draft identity the surface supplied, reused by the save
                // that follows so a person who probes an address and then
                // saves it gets one row rather than two (decision 0096
                // section 5).
                provider_id: value.provider_id,
            });
        }
        wire::CoreServiceCommandKind::SetProviderModelPreference => {
            // Absent, never a member standing for absence, on both halves. The
            // plane resolves the model identity against the merged catalog and
            // refuses a name no provider carries, so nothing is parsed into a
            // type here that the catalog would then have to disagree with
            // (decision 0093 section 4).
            let thinking = if value.has_thinking_level {
                Some(wire::ThinkingPreference {
                    level: wire::ThinkingLevel::from_wire(u32::from(value.thinking_level))?,
                })
            } else {
                None
            };
            command.set_provider_model_preference = Some(wire::SetProviderModelPreferenceCommand {
                provider_id: value.provider_id,
                model_id: value.has_model_id.then_some(value.model_id),
                thinking,
            });
        }
        wire::CoreServiceCommandKind::StartTask
        | wire::CoreServiceCommandKind::CancelTask
        | wire::CoreServiceCommandKind::UserDecision
        | wire::CoreServiceCommandKind::AuthCallback
        | wire::CoreServiceCommandKind::PermissionResult
        | wire::CoreServiceCommandKind::StartAuth
        | wire::CoreServiceCommandKind::RequestEmailLink
        | wire::CoreServiceCommandKind::SignOut
        | wire::CoreServiceCommandKind::AuthCredentialResult
        | wire::CoreServiceCommandKind::CorrectWorkspaceFact
        | wire::CoreServiceCommandKind::ExcludeWorkspaceSource
        | wire::CoreServiceCommandKind::RequestWorkspaceExport
        | wire::CoreServiceCommandKind::SaveWorkspace
        | wire::CoreServiceCommandKind::RenameWorkspace
        | wire::CoreServiceCommandKind::DeleteWorkspace
        | wire::CoreServiceCommandKind::DiscardWorkspace
        | wire::CoreServiceCommandKind::SearchLibrary
        | wire::CoreServiceCommandKind::SaveLibraryFact
        | wire::CoreServiceCommandKind::RemoveLibraryEntry
        | wire::CoreServiceCommandKind::RequestLibraryExport
        | wire::CoreServiceCommandKind::SearchMemory
        | wire::CoreServiceCommandKind::UpsertMemory
        | wire::CoreServiceCommandKind::DeleteMemory
        | wire::CoreServiceCommandKind::AcceptTaskArtifact
        | wire::CoreServiceCommandKind::ExportTaskArtifact
        | wire::CoreServiceCommandKind::SetAssetDeliveryPolicy
        | wire::CoreServiceCommandKind::RequestAsset
        | wire::CoreServiceCommandKind::RemoveAsset
        | wire::CoreServiceCommandKind::CompleteHandover
        | wire::CoreServiceCommandKind::ExpireHandover
        | wire::CoreServiceCommandKind::SupplyUserInput
        | wire::CoreServiceCommandKind::FollowUp
        | wire::CoreServiceCommandKind::SupplyFieldValues
        // Kinds another seam carries. They are refused here rather than
        // omitted from the match, so a kind added to the contract is a compile
        // error in every file that has to decide about it instead of a silent
        // fallthrough. Both composer kinds are composed and withdrawn by the
        // profile composition and reach the core through their own seam
        // (decisions 0097 and 0098).
        | wire::CoreServiceCommandKind::RequestComposerCompletion
        | wire::CoreServiceCommandKind::CancelComposerCompletion
        | wire::CoreServiceCommandKind::PauseTask
        | wire::CoreServiceCommandKind::ResumeTask
        | wire::CoreServiceCommandKind::TakeOver
        | wire::CoreServiceCommandKind::SetAssistantConfiguration
        | wire::CoreServiceCommandKind::MutateSkill
        | wire::CoreServiceCommandKind::ReplaceSavedDataSnapshot => return None,
    }
    command.has_valid_body().then_some(command)
}

/// One flat model record, back into the contract's own.
///
/// Total rather than fallible: every field is a plain value the contract
/// carries as it stands, so there is nothing here to disagree with and nothing
/// to refuse. What the endpoint said about a model is judged by the plane that
/// files it, not by this seam.
pub(crate) fn custom_model_to_wire(
    value: provider_ffi::BridgeCustomModel,
) -> wire::CustomModelSpec {
    wire::CustomModelSpec {
        model_id: value.model_id,
        display_name: value.display_name,
        context_window: value.context_window,
        max_output_tokens: value.max_output_tokens,
        reasoning: value.reasoning,
        tool_calling: value.tool_calling,
    }
}

/// Which admission status one refusal is.
///
/// Every provider refusal is `INVALID_COMMAND`, and that is a statement rather
/// than a shrug: none of them is a race, a deadline or a backpressure signal.
/// The reason a person needs is a fact about *their* provider — this key is
/// for a vendor that does not take keys, that identity is already the
/// catalog's — and the Core API status enumeration cannot carry one. The named
/// reason lives on the refusal type this function consumes, and the arms are
/// spelled out so that a status which ever stops being uniform is a compile
/// error here rather than a wrong answer at a surface.
fn error_status(error: ProviderServiceError) -> u8 {
    match error {
        ProviderServiceError::UnsupportedKind
        | ProviderServiceError::MissingBody
        | ProviderServiceError::ReservedWireApi
        | ProviderServiceError::Domain(ProviderError::EmptyProviderId)
        | ProviderServiceError::Domain(ProviderError::ProviderIdTooLong)
        | ProviderServiceError::Domain(ProviderError::InvalidProviderId)
        | ProviderServiceError::Domain(ProviderError::EmptyDisplayName)
        | ProviderServiceError::Domain(ProviderError::DisplayNameTooLong)
        | ProviderServiceError::Domain(ProviderError::EmptyCredentialHandle)
        | ProviderServiceError::Domain(ProviderError::CredentialHandleTooLong)
        | ProviderServiceError::Domain(ProviderError::InvalidEndpoint)
        | ProviderServiceError::Domain(ProviderError::TooManyCustomProviders)
        | ProviderServiceError::Domain(ProviderError::TooManyCustomModels)
        | ProviderServiceError::Domain(ProviderError::UnknownProvider)
        | ProviderServiceError::Domain(ProviderError::ProviderIdReserved)
        | ProviderServiceError::Domain(ProviderError::MethodNotOffered)
        | ProviderServiceError::Domain(ProviderError::ProviderDisabled)
        | ProviderServiceError::Domain(ProviderError::FlowAlreadyRunning)
        | ProviderServiceError::Domain(ProviderError::UnknownFlow)
        | ProviderServiceError::Domain(ProviderError::StateMismatch)
        | ProviderServiceError::Domain(ProviderError::FlowExpired)
        | ProviderServiceError::Domain(ProviderError::TooManyPendingFlows)
        | ProviderServiceError::Domain(ProviderError::MalformedRedirect) => invalid(),
    }
}

/// Which admission status one probe refusal is.
///
/// A probe already in flight is honest backpressure — the verdict the flight
/// will produce is the answer the second ask wants, and asking again after it
/// settles is the remedy. Everything else is a fact about the command itself.
/// A provider with nothing to probe with is neither: it is a fact about the
/// catalog, and the core files it as a verdict instead of refusing, so no
/// member here stands for it.
///
/// Shared with the endpoint probe, which claims the same single flight and so
/// must refuse a second ask on the same terms. Two mappings for one flight is
/// how one sheet ends up being told two different things about one refusal.
pub(crate) fn probe_error_status(error: core_runtime::ProbeCommandError) -> u8 {
    match error {
        core_runtime::ProbeCommandError::ProbeInFlight => wire::AdmissionStatus::Backpressure as u8,
        core_runtime::ProbeCommandError::IdentityExhausted => unavailable(),
        core_runtime::ProbeCommandError::InvalidCommand
        | core_runtime::ProbeCommandError::UnknownProvider => invalid(),
    }
}

const fn invalid() -> u8 {
    wire::AdmissionStatus::InvalidCommand as u8
}

const fn unavailable() -> u8 {
    wire::AdmissionStatus::CoreUnavailable as u8
}

/// The provider bridge's mirror of the operation record, into the one wire
/// envelope. Field for field, because the mirror exists only to break a
/// header cycle between generated cxx bridges, never to differ.
fn provider_operation_to_wire(
    value: provider_ffi::BridgeProviderOperation,
) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}

/// The operation envelope, checked before the plane is asked anything.
///
/// The same shape the delivery bridge uses, and for the same reason: a
/// provider command belongs to no task, so a non-zero task revision is a
/// caller that built the envelope wrong rather than a stale one.
fn validate_operation(
    operation: &provider_ffi::BridgeProviderOperation,
    generation: u64,
    now_monotonic_ms: u64,
) -> Result<(), u8> {
    if operation.service_generation != generation {
        return Err(wire::AdmissionStatus::StaleGeneration as u8);
    }
    if operation.task_revision != 0
        || operation.operation_id.is_empty()
        || operation.operation_id.len() > wire::MAX_OPERATION_ID_BYTES
        || operation.idempotency_key.is_empty()
        || operation.idempotency_key.len() > wire::MAX_IDEMPOTENCY_KEY_BYTES
    {
        return Err(invalid());
    }
    if now_monotonic_ms >= operation.deadline_monotonic_ms {
        return Err(wire::AdmissionStatus::DeadlineExceeded as u8);
    }
    Ok(())
}
