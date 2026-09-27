// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Sequenced generated Core API status publication for one service generation.

use crate::ffi;
use crate::service_bridge_runtime::ServiceBridge;
use crate::service_bridge_task_effect::project_restored_effects;

pub(crate) fn next_state(bridge: &mut ServiceBridge) -> Result<ffi::BridgeState, ()> {
    let model_artifacts = bridge
        .runtime
        .as_ref()
        .ok_or(())?
        .core()
        .model_artifact_registrations()
        .map_err(|_| ())?
        .into_iter()
        .map(|artifact| ffi::BridgeModelArtifactRegistration {
            asset_id: artifact.asset_id,
            asset_revision: artifact.asset_revision,
            asset_kind: match artifact.asset_kind {
                core_runtime::wire::AssetKind::PythonStdlib => 0,
                core_runtime::wire::AssetKind::PythonPackages => 1,
                core_runtime::wire::AssetKind::ModelWeights => 2,
                core_runtime::wire::AssetKind::ModelTokenizer => 3,
                core_runtime::wire::AssetKind::FilterList => 4,
                core_runtime::wire::AssetKind::CountryFlags => 5,
                core_runtime::wire::AssetKind::StartScenes => 6,
            },
            format: match artifact.format {
                core_runtime::wire::ToolModelArtifactKind::LitertTflite => 0,
                core_runtime::wire::ToolModelArtifactKind::OnnxRuntime => 1,
                core_runtime::wire::ToolModelArtifactKind::Gguf => 2,
            },
            adapter: artifact.adapter,
            byte_length: artifact.byte_length,
            digest: artifact.digest,
        })
        .collect();
    let status = bridge
        .runtime
        .as_ref()
        .ok_or(())?
        .encode_ready_core_status()
        .map_err(|_| ())?;
    let accepted_task_consents = status
        .accepted_task_consents
        .into_iter()
        .map(|consent| {
            Ok(ffi::BridgeAcceptedTaskConsent {
                task_id: consent.task_id,
                service_generation: bridge.generation.value(),
                current_task_revision: consent.current_task_revision,
                accepted_revision: consent.accepted_revision,
                browser_session_id: consent.browser_session_id,
                receipt_id: consent.receipt_id,
                sources: consent
                    .sources
                    .into_iter()
                    .map(|source| ffi::BridgeConsentSource {
                        source_id: source.source_id.to_text(),
                        tab_id: source.tab_id.0,
                        normalized_origin: source.normalized_origin,
                        has_canonical_locator: source.canonical_locator.is_some(),
                        canonical_locator: source.canonical_locator.unwrap_or_default(),
                    })
                    .collect(),
                source_discovery_enabled: consent.source_discovery_enabled,
                new_source_cap: consent.new_source_cap,
                provider_route: provider_route(consent.provider_route_id.as_deref())?,
            })
        })
        .collect::<Result<Vec<_>, ()>>()?;
    let restored = core::mem::take(&mut bridge.restored_task_effects);
    let mut task_effects = Vec::new();
    for batch in restored {
        let revision_matches = bridge
            .runtime
            .as_ref()
            .and_then(|runtime| runtime.core().task(&batch.task_id))
            .is_some_and(|task| task.revision() == batch.task_revision);
        if !revision_matches || task_effects.len() >= core_runtime::wire::MAX_TASK_EFFECTS_PER_STATE
        {
            return Err(());
        }
        let ordinal = u32::try_from(task_effects.len()).map_err(|_| ())?;
        task_effects.extend(project_restored_effects(bridge, batch, ordinal)?);
        if task_effects.len() > core_runtime::wire::MAX_TASK_EFFECTS_PER_STATE {
            return Err(());
        }
    }
    let sequence = bridge.state_sequence.checked_add(1).ok_or(())?;
    bridge.state_sequence = sequence;
    Ok(ffi::BridgeState {
        service_generation: bridge.generation.value(),
        sequence,
        core_status_schema_version: status.schema_version,
        payload: status.payload,
        model_artifacts,
        task_revisions: status
            .task_revisions
            .into_iter()
            .map(|task| ffi::BridgeTaskRevision {
                task_id: task.task_id,
                service_generation: bridge.generation.value(),
                task_revision: task.task_revision,
                allowed_controls: task
                    .allowed_controls
                    .into_iter()
                    .map(|control| match control {
                        core_runtime::TaskControlKind::Pause => 0,
                        core_runtime::TaskControlKind::Resume => 1,
                        core_runtime::TaskControlKind::TakeOver => 2,
                        core_runtime::TaskControlKind::Stop => 3,
                    })
                    .collect(),
            })
            .collect(),
        pending_approvals: status
            .pending_approvals
            .into_iter()
            .map(|approval| ffi::BridgePendingApproval {
                task_id: approval.task_id,
                action_id: approval.action_id,
                proposal_digest: approval.proposal_digest,
                service_generation: bridge.generation.value(),
                task_revision: approval.task_revision,
            })
            .collect(),
        task_settlements: status
            .task_settlements
            .into_iter()
            .map(|settlement| ffi::BridgeTaskSettlement {
                task_id: settlement.task_id,
                service_generation: bridge.generation.value(),
                task_revision: settlement.task_revision,
                kind: match settlement.kind {
                    core_runtime::runtime::TaskSettlementKind::Pause => 0,
                    core_runtime::runtime::TaskSettlementKind::Cancel => 1,
                },
            })
            .collect(),
        pending_permissions: status
            .pending_permissions
            .into_iter()
            .map(|permission| ffi::BridgePendingPermission {
                task_id: permission.task_id,
                request_id: permission.request_id,
                permission: match permission.permission {
                    core_runtime::PlatformPermission::Notifications => 0,
                    core_runtime::PlatformPermission::Microphone => 1,
                    core_runtime::PlatformPermission::Camera => 2,
                    core_runtime::PlatformPermission::Location => 3,
                },
                service_generation: bridge.generation.value(),
                task_revision: permission.task_revision,
                deadline_monotonic_ms: permission.deadline_monotonic_ms,
                deadline_utc_ms: permission.deadline_utc_ms,
                browser_session_id: permission.browser_session_id,
            })
            .collect(),
        terminal_tasks: status
            .terminal_tasks
            .into_iter()
            .map(|terminal| ffi::BridgeTerminalTask {
                task_id: terminal.task_id,
                service_generation: bridge.generation.value(),
                task_revision: terminal.task_revision,
                kind: match terminal.kind {
                    core_runtime::TerminalTaskKind::Completed => 0,
                    core_runtime::TerminalTaskKind::Partial => 1,
                    core_runtime::TerminalTaskKind::Failed => 2,
                    core_runtime::TerminalTaskKind::Cancelled => 3,
                },
            })
            .collect(),
        accepted_task_consents,
        committed_action_approvals: status
            .committed_action_approvals
            .into_iter()
            .map(|approval| ffi::BridgeCommittedActionApproval {
                task_id: approval.task_id,
                action_id: approval.action_id,
                service_generation: bridge.generation.value(),
                committed_revision: approval.committed_revision,
                receipt_id: approval.receipt_id,
                proposal_digest: approval.proposal_digest,
                expires_at_monotonic_ms: approval.expires_at_monotonic_ms,
                expires_at_utc_ms: approval.expires_at_utc_ms,
                browser_session_id: approval.browser_session_id,
            })
            .collect(),
        task_effects,
    })
}

fn provider_route(route: Option<&str>) -> Result<u8, ()> {
    match route {
        Some("direct_user_key") => Ok(core_runtime::wire::TaskProviderRoute::DirectUserKey as u8),
        Some("managed_service") => Ok(core_runtime::wire::TaskProviderRoute::ManagedService as u8),
        Some(core_runtime::REVIEWED_NO_MODEL_ROUTE_ID) => {
            Ok(core_runtime::wire::TaskProviderRoute::NoModelRequired as u8)
        }
        None | Some(_) => Err(()),
    }
}

pub(crate) fn response_after_task_change(
    bridge: &mut ServiceBridge,
    mut response: ffi::BridgeResponse,
    task_effects: Vec<ffi::BridgeTaskEffect>,
) -> ffi::BridgeResponse {
    response.states = state_after_change(bridge);
    let Some(state) = response.states.first_mut() else {
        response.admission.status = 6;
        withdraw_effects(&mut response);
        return response;
    };
    state.task_effects = task_effects;
    response
}

/// Publishes one complete immutable state or poisons the corrupt runtime.
pub(crate) fn state_after_change(bridge: &mut ServiceBridge) -> Vec<ffi::BridgeState> {
    match next_state(bridge) {
        Ok(state) => vec![state],
        Err(()) => {
            bridge.runtime = None;
            Vec::new()
        }
    }
}

pub(crate) fn response_after_change(
    bridge: &mut ServiceBridge,
    mut response: ffi::BridgeResponse,
) -> ffi::BridgeResponse {
    response.states = state_after_change(bridge);
    if response.states.is_empty() {
        response.admission.status = 6;
        withdraw_effects(&mut response);
    }
    response
}

/// Withdraws every effect when state projection has torn down its runtime.
///
/// The exhaustive destructure is the rule: adding a vector to
/// `BridgeResponse` must make this function stop compiling until its failure
/// behavior is decided. A hand-maintained subset previously let four effect
/// families escape after `state_after_change` had discarded the runtime that
/// could receive their results.
fn withdraw_effects(response: &mut ffi::BridgeResponse) {
    let ffi::BridgeResponse {
        admission: _,
        storage_effects,
        workspace_effects,
        account_effects,
        asset_effects,
        probe_effects,
        listing_effects,
        endpoint_probe_effects,
        task_answer_events: _,
        states: _,
    } = response;
    storage_effects.clear();
    workspace_effects.clear();
    account_effects.clear();
    asset_effects.clear();
    probe_effects.clear();
    listing_effects.clear();
    endpoint_probe_effects.clear();
}
