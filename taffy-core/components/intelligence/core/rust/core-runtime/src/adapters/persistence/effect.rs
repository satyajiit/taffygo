// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reducer effect-intent projections committed before broker dispatch.

use bip_types::identity::{ActionId, SemanticNodeId, TabId};
use core_service_types as wire;
use task_engine::field_values::{FieldNodeIds, FieldValueRequestId};
use task_engine::Effect;

use super::enum_action::{tool_runtime, untool_runtime};
use super::enum_journal::{
    command_kind, recovery, revocation, uncommand_kind, unrecovery, unrevocation,
};
use super::enum_permission::{permission, unpermission};
use super::enum_task::{artifact, unartifact};
use super::value::artifact_id as restored_artifact_id;
use super::ConversionError;

pub(super) fn effect(value: &Effect) -> wire::PersistedEffectIntent {
    match value {
        Effect::RevokeAuthority { reason } => wire::PersistedEffectIntent::RevokeAuthority {
            reason: revocation(*reason),
        },
        Effect::AskPolicy { action_id } => wire::PersistedEffectIntent::AskPolicy {
            action_id: action_id.0.clone(),
        },
        Effect::RequestApproval { action_id } => wire::PersistedEffectIntent::RequestApproval {
            action_id: action_id.0.clone(),
        },
        Effect::PrepareDiscoveryTab {
            browser_session_id,
            remaining_new_source_cap,
        } => wire::PersistedEffectIntent::PrepareDiscoveryTab {
            browser_session_id: browser_session_id.as_str().to_owned(),
            remaining_new_source_cap: *remaining_new_source_cap,
        },
        Effect::RequestPermission {
            request_id,
            permission: permission_kind,
            deadline_monotonic_ms,
            deadline_utc_ms,
            browser_session_id,
        } => wire::PersistedEffectIntent::RequestPermission {
            request_id: request_id.as_str().to_owned(),
            permission: permission(*permission_kind),
            deadline_monotonic_ms: *deadline_monotonic_ms,
            deadline_utc_ms: *deadline_utc_ms,
            browser_session_id: browser_session_id.as_str().to_owned(),
        },
        Effect::DispatchAction { action_id } => wire::PersistedEffectIntent::DispatchAction {
            action_id: action_id.0.clone(),
        },
        Effect::RunToolJob {
            action_id,
            job_id,
            runtime,
        } => wire::PersistedEffectIntent::RunToolJob {
            action_id: action_id.0.clone(),
            job_id: job_id.as_str().to_owned(),
            runtime: tool_runtime(*runtime),
        },
        Effect::RunLibraryTool { action_id } => wire::PersistedEffectIntent::RunLibraryTool {
            action_id: action_id.0.clone(),
        },
        Effect::RunMemoryTool { action_id } => wire::PersistedEffectIntent::RunMemoryTool {
            action_id: action_id.0.clone(),
        },
        Effect::AwaitInFlightWork { settle_with } => {
            wire::PersistedEffectIntent::AwaitInFlightWork {
                settle_with: command_kind(*settle_with),
            }
        }
        Effect::ReconcileAction { action_id, rule } => {
            wire::PersistedEffectIntent::ReconcileAction {
                action_id: action_id.0.clone(),
                rule: recovery(*rule),
            }
        }
        Effect::ReleaseTaskTabs => wire::PersistedEffectIntent::ReleaseTaskTabs,
        Effect::GenerateArtifact {
            artifact_id,
            kind,
            workspace_revision,
        } => wire::PersistedEffectIntent::GenerateArtifact {
            artifact_id: artifact_id.as_str().to_owned(),
            kind: artifact(*kind),
            workspace_revision: *workspace_revision,
        },
        Effect::ExportArtifact {
            artifact_id,
            kind,
            workspace_revision,
        } => wire::PersistedEffectIntent::ExportArtifact {
            artifact_id: artifact_id.as_str().to_owned(),
            kind: artifact(*kind),
            workspace_revision: *workspace_revision,
        },
        Effect::CallModel { call_id } => wire::PersistedEffectIntent::CallModel {
            call_id: call_id.as_str().to_owned(),
        },
        // The companions are not journalled (decision 0238). An intent
        // re-issued after a restart asks about the named line alone, which is
        // what every request asked before them; the durable format is not
        // widened for a sheet that is at worst one row shorter.
        Effect::RequestFieldValues {
            request_id,
            tab_id,
            node_id,
            companion_node_ids: _,
        } => wire::PersistedEffectIntent::RequestFieldValues {
            request_id: request_id.as_str().to_owned(),
            tab_id: tab_id.0.clone(),
            node_id: node_id.0.clone(),
        },
        Effect::AwaitHandover {
            handover_id,
            window_ms,
        } => wire::PersistedEffectIntent::AwaitHandover {
            handover_id: handover_id.as_str().to_owned(),
            window_ms: *window_ms,
        },
    }
}

pub(super) fn uneffect(value: wire::PersistedEffectIntent) -> Result<Effect, ConversionError> {
    Ok(match value {
        wire::PersistedEffectIntent::RevokeAuthority { reason } => Effect::RevokeAuthority {
            reason: unrevocation(reason),
        },
        wire::PersistedEffectIntent::AskPolicy { action_id } => Effect::AskPolicy {
            action_id: action(action_id)?,
        },
        wire::PersistedEffectIntent::RequestApproval { action_id } => Effect::RequestApproval {
            action_id: action(action_id)?,
        },
        wire::PersistedEffectIntent::PrepareDiscoveryTab {
            browser_session_id,
            remaining_new_source_cap,
        } => restore_discovery_tab(browser_session_id, remaining_new_source_cap)?,
        wire::PersistedEffectIntent::RequestPermission {
            request_id,
            permission,
            deadline_monotonic_ms,
            deadline_utc_ms,
            browser_session_id,
        } => restore_permission(
            request_id,
            permission,
            deadline_monotonic_ms,
            deadline_utc_ms,
            browser_session_id,
        )?,
        wire::PersistedEffectIntent::DispatchAction { action_id } => Effect::DispatchAction {
            action_id: action(action_id)?,
        },
        wire::PersistedEffectIntent::RunToolJob {
            action_id,
            job_id,
            runtime,
        } => Effect::RunToolJob {
            action_id: action(action_id)?,
            job_id: task_engine::ToolJobId::new(job_id),
            runtime: untool_runtime(runtime),
        },
        wire::PersistedEffectIntent::RunLibraryTool { action_id } => Effect::RunLibraryTool {
            action_id: action(action_id)?,
        },
        wire::PersistedEffectIntent::RunMemoryTool { action_id } => Effect::RunMemoryTool {
            action_id: action(action_id)?,
        },
        wire::PersistedEffectIntent::AwaitInFlightWork { settle_with } => {
            Effect::AwaitInFlightWork {
                settle_with: uncommand_kind(settle_with)?,
            }
        }
        wire::PersistedEffectIntent::ReconcileAction { action_id, rule } => {
            Effect::ReconcileAction {
                action_id: action(action_id)?,
                rule: unrecovery(rule),
            }
        }
        wire::PersistedEffectIntent::ReleaseTaskTabs => Effect::ReleaseTaskTabs,
        wire::PersistedEffectIntent::GenerateArtifact {
            artifact_id,
            kind,
            workspace_revision,
        } => Effect::GenerateArtifact {
            artifact_id: restored_artifact_id(artifact_id)?,
            kind: unartifact(kind),
            workspace_revision: restore_workspace_revision(workspace_revision)?,
        },
        wire::PersistedEffectIntent::ExportArtifact {
            artifact_id,
            kind,
            workspace_revision,
        } => Effect::ExportArtifact {
            artifact_id: restored_artifact_id(artifact_id)?,
            kind: unartifact(kind),
            workspace_revision: restore_workspace_revision(workspace_revision)?,
        },
        // Restored, now that a reducer emits it. The intent is the durable
        // half of a paid call: the browser journals this identity before it
        // dispatches and refuses one it has already journalled, which is what
        // a restart needs in order to resume rather than re-spend.
        wire::PersistedEffectIntent::CallModel { call_id } => Effect::CallModel {
            call_id: model_call(call_id)?,
        },
        // The window is restored as it was written and is not recomputed from
        // today's constant. A restart that shortened a window the person was
        // already inside would end their handover early, and one that
        // lengthened it would claim a promise nobody made.
        wire::PersistedEffectIntent::RequestFieldValues {
            request_id,
            tab_id,
            node_id,
        } => Effect::RequestFieldValues {
            request_id: FieldValueRequestId::new(request_id)
                .map_err(|_| ConversionError::InvalidIdentifier)?,
            tab_id: TabId::new(tab_id),
            node_id: SemanticNodeId::new(node_id),
            companion_node_ids: FieldNodeIds::none(),
        },
        wire::PersistedEffectIntent::AwaitHandover {
            handover_id,
            window_ms,
        } => Effect::AwaitHandover {
            handover_id: task_engine::HandoverId::new(handover_id)
                .map_err(|_| ConversionError::InvalidIdentifier)?,
            window_ms,
        },
    })
}

fn restore_discovery_tab(
    browser_session_id: String,
    remaining_new_source_cap: u32,
) -> Result<Effect, ConversionError> {
    if remaining_new_source_cap == 0
        || remaining_new_source_cap > task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP
    {
        return Err(ConversionError::InvalidValue);
    }
    Ok(Effect::PrepareDiscoveryTab {
        browser_session_id: task_engine::BrowserSessionId::new(browser_session_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
        remaining_new_source_cap,
    })
}

fn restore_permission(
    request_id: String,
    permission: wire::PersistedPlatformPermission,
    deadline_monotonic_ms: u64,
    deadline_utc_ms: u64,
    browser_session_id: String,
) -> Result<Effect, ConversionError> {
    Ok(Effect::RequestPermission {
        request_id: task_engine::PermissionRequestId::new(request_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
        permission: unpermission(permission),
        deadline_monotonic_ms,
        deadline_utc_ms,
        browser_session_id: task_engine::BrowserSessionId::new(browser_session_id)
            .map_err(|_| ConversionError::InvalidIdentifier)?,
    })
}

fn restore_workspace_revision(value: u64) -> Result<u64, ConversionError> {
    (value != 0)
        .then_some(value)
        .ok_or(ConversionError::InvalidValue)
}

fn model_call(value: String) -> Result<task_engine::ModelCallId, ConversionError> {
    if value.is_empty() {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(task_engine::ModelCallId::new(value))
}

fn action(value: String) -> Result<ActionId, ConversionError> {
    if value.is_empty() {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(ActionId(value))
}
