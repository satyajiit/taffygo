// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One reducer effect projected into its exact flat browser binding.

use core_runtime::ports::ActionEffectFacts;
use core_runtime::wire;
use core_runtime::{ComposedModelTurn, Effect, TaskId};

use super::super::action_projection::{project_action, project_approval, project_policy};
use super::super::enums::{action_operation, artifact, platform_permission, recovery, revocation};
use super::super::model::project_model;
use super::empty::empty_binding;
use crate::ffi;
use crate::service_bridge_runtime::ServiceBridge;

pub(super) fn project_effect(
    bridge: &ServiceBridge,
    task_id: &TaskId,
    ordinal: u32,
    effect_id: String,
    operation: wire::OperationEnvelope,
    effect: Effect,
    action: Option<ActionEffectFacts>,
    model: Option<&ComposedModelTurn>,
    artifact_content: Vec<u8>,
) -> Result<ffi::BridgeTaskEffect, ()> {
    let mut out = empty_binding(task_id, ordinal, effect_id, operation);
    match effect {
        Effect::RevokeAuthority { reason } => {
            out.kind = wire::TaskReducerEffectKind::RevokeAuthority as u8;
            out.recovery_rule = revocation(reason) as u8;
        }
        Effect::AskPolicy { .. } => {
            out.kind = wire::TaskReducerEffectKind::AskPolicy as u8;
            project_policy(bridge, task_id, &mut out, action.ok_or(())?)?;
        }
        Effect::RequestApproval { .. } => {
            out.kind = wire::TaskReducerEffectKind::RequestApproval as u8;
            project_approval(&mut out, action.ok_or(())?)?;
        }
        Effect::PrepareDiscoveryTab {
            browser_session_id,
            remaining_new_source_cap,
        } => {
            if browser_session_id.as_str().is_empty()
                || remaining_new_source_cap == 0
                || remaining_new_source_cap > core_runtime::MAX_WEB_ERRAND_NEW_SOURCE_CAP
            {
                return Err(());
            }
            out.kind = wire::TaskReducerEffectKind::PrepareDiscoveryTab as u8;
            out.discovery_bootstrap_browser_session_id = browser_session_id.as_str().to_owned();
            out.discovery_bootstrap_remaining_new_source_cap = remaining_new_source_cap;
        }
        Effect::RequestPermission {
            request_id,
            permission,
            deadline_monotonic_ms,
            deadline_utc_ms,
            browser_session_id,
        } => {
            out.kind = wire::TaskReducerEffectKind::RequestPermission as u8;
            out.request_id = request_id.as_str().to_owned();
            out.permission = platform_permission(permission) as u8;
            out.deadline_monotonic_ms = deadline_monotonic_ms;
            out.deadline_utc_ms = deadline_utc_ms;
            out.permission_browser_session_id = browser_session_id.as_str().to_owned();
        }
        Effect::DispatchAction { .. } => {
            out.kind = wire::TaskReducerEffectKind::DispatchAction as u8;
            project_action(bridge, task_id, &mut out, action.ok_or(())?)?;
        }
        // The dispatched half of a tool-job proposal carries exactly the
        // durable facts the reducer owns: the action whose authority it
        // spends, the job identity derived from the task and that action, and
        // the runtime family the registered tool named. The job body the
        // broker validates is composed once a registered tool defines what to
        // encode; until an executor exists the browser answers this binding
        // with one correlated unavailable terminal, which returns through
        // `complete_tool_job` as the durable outcome it honestly is.
        Effect::RunToolJob {
            action_id,
            job_id,
            runtime,
        } => {
            out.kind = wire::TaskReducerEffectKind::RunToolJob as u8;
            out.action_id = action_id.as_str().to_owned();
            out.job_id = job_id.as_str().to_owned();
            out.tool_runtime = tool_runtime_wire(runtime) as u8;
            if runtime == core_runtime::ToolRuntime::Python {
                let facts = action.ok_or(())?;
                if facts.action_id != action_id.as_str() {
                    return Err(());
                }
                let body = super::python::compose(bridge, task_id, &facts)?;
                out.has_python_job = true;
                out.python_entrypoint = body.entrypoint;
                out.python_input = body.input;
            } else if runtime == core_runtime::ToolRuntime::Media {
                let facts = action.ok_or(())?;
                if facts.action_id != action_id.as_str() {
                    return Err(());
                }
                let body = super::media::compose(bridge, &facts)?;
                out.has_media_job = true;
                out.media_operation = body.operation;
                out.media_source_id = body.source_id;
                out.media_source_browser_session_id = body.source_browser_session_id;
                out.media_source_bytes = body.source_bytes;
                out.media_max_frames = body.max_frames;
            }
        }
        Effect::RunLibraryTool { action_id } => {
            let facts = action.ok_or(())?;
            if facts.action_id != action_id.as_str()
                || !matches!(
                    facts.proposal.intent(),
                    core_runtime::ActionIntent::Library(_)
                )
            {
                return Err(());
            }
            out.kind = wire::TaskReducerEffectKind::RunLibraryTool as u8;
            out.action_id = action_id.as_str().to_owned();
            out.action_operation = action_operation(facts.proposal.intent()) as u8;
        }
        Effect::RunMemoryTool { action_id } => {
            let facts = action.ok_or(())?;
            if facts.action_id != action_id.as_str()
                || !matches!(
                    facts.proposal.intent(),
                    core_runtime::ActionIntent::Memory(_)
                )
            {
                return Err(());
            }
            out.kind = wire::TaskReducerEffectKind::RunMemoryTool as u8;
            out.action_id = action_id.as_str().to_owned();
            out.action_operation = action_operation(facts.proposal.intent()) as u8;
        }
        Effect::AwaitInFlightWork { settle_with } => {
            out.kind = wire::TaskReducerEffectKind::AwaitInFlightWork as u8;
            out.settlement_kind = match settle_with {
                core_runtime::CommandKind::PauseSettled => wire::TaskSettlementKind::Pause as u8,
                core_runtime::CommandKind::CancelSettled => wire::TaskSettlementKind::Cancel as u8,
                _ => return Err(()),
            };
        }
        Effect::ReconcileAction { action_id, rule } => {
            out.kind = wire::TaskReducerEffectKind::ReconcileAction as u8;
            let facts = action.ok_or(())?;
            if facts.action_id != action_id.as_str() {
                return Err(());
            }
            out.action_id = facts.action_id;
            out.dispatch_id = facts.dispatch_id.ok_or(())?;
            out.action_operation = action_operation(facts.proposal.intent()) as u8;
            out.recovery_rule = recovery(rule) as u8;
        }
        Effect::ReleaseTaskTabs => {
            out.kind = wire::TaskReducerEffectKind::ReleaseTaskTabs as u8;
        }
        Effect::GenerateArtifact {
            artifact_id,
            kind,
            workspace_revision,
        } => {
            out.kind = wire::TaskReducerEffectKind::GenerateArtifact as u8;
            out.artifact_kind = artifact(kind) as u8;
            out.artifact_id = artifact_id.as_str().to_owned();
            out.artifact_workspace_revision = workspace_revision;
        }
        Effect::ExportArtifact {
            artifact_id,
            kind,
            workspace_revision,
        } => {
            out.kind = wire::TaskReducerEffectKind::ExportArtifact as u8;
            out.artifact_kind = artifact(kind) as u8;
            out.artifact_id = artifact_id.as_str().to_owned();
            out.artifact_workspace_revision = workspace_revision;
            out.artifact_content = artifact_content;
        }
        // The plan was composed before this function was entered, by
        // `model::compose_model_turn`, which is the one place holding the
        // profile it needs. Arriving here without one is a defect in that
        // caller rather than a fact about the task, and it is refused: an
        // empty model effect would hand the browser a paid call with no plan,
        // and the browser would have every reason to send it.
        Effect::CallModel { call_id } => {
            out.kind = wire::TaskReducerEffectKind::CallModel as u8;
            project_model(&mut out, &call_id, model.ok_or(())?)?;
        }
        Effect::AwaitHandover {
            handover_id,
            window_ms,
        } => {
            out.kind = wire::TaskReducerEffectKind::AwaitHandover as u8;
            out.handover_id = handover_id.as_str().to_owned();
            // The window the reducer decided, carried across unchanged. The
            // browser adds it to the moment it opens the surface; it does not
            // get to choose how long a person has.
            out.handover_window_ms = window_ms;
        }
        // The form and nothing inside it. Which of its fields need a person,
        // and what kind of thing each is, is the browser's judgement, made
        // from the classification it re-reads at dispatch — so there is
        // nothing to project here but where to look (decision 0088). A field
        // named on its own brings the page's other person-only fields as
        // further places to look, never as values (decision 0238).
        Effect::RequestFieldValues {
            request_id,
            tab_id,
            node_id,
            companion_node_ids,
        } => {
            out.kind = wire::TaskReducerEffectKind::RequestFieldValues as u8;
            out.request_id = request_id.as_str().to_owned();
            out.tab_id = tab_id.0.clone();
            out.node_id = node_id.0.clone();
            out.has_node_id = true;
            out.field_values_companion_node_ids = companion_node_ids
                .as_slice()
                .iter()
                .map(|id| id.0.clone())
                .collect();
        }
    }
    Ok(out)
}

const fn tool_runtime_wire(value: core_runtime::ToolRuntime) -> wire::ToolRuntimeKind {
    match value {
        core_runtime::ToolRuntime::Media => wire::ToolRuntimeKind::Media,
        core_runtime::ToolRuntime::Python => wire::ToolRuntimeKind::Python,
        core_runtime::ToolRuntime::LocalModel => wire::ToolRuntimeKind::LocalModel,
        core_runtime::ToolRuntime::Wasm => wire::ToolRuntimeKind::Wasm,
    }
}
