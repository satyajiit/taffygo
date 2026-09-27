// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Stable planning and bookkeeping for the durable skill-run ledger.

use core_runtime::wire;
use core_runtime::TaskId;

use crate::ffi;

use super::ServiceBridge;

pub(crate) struct PlannedSkillRun {
    pub(crate) operation_id: String,
    pub(crate) task_id: String,
    pub(crate) effect: ffi::BridgeStorageEffect,
}

pub(crate) struct PendingSkillRun {
    pub(super) effect_id: String,
    pub(super) service_generation: u64,
    pub(super) task_revision: u64,
    pub(super) deadline_monotonic_ms: u64,
    pub(super) idempotency_key: String,
    pub(super) task_id: String,
}

/// Plans one stable, idempotent run-ledger write for a newly terminal task.
pub(crate) fn plan_skill_run(
    bridge: &ServiceBridge,
    task_id: &TaskId,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> Result<Option<PlannedSkillRun>, ()> {
    if bridge.recorded_skill_run_tasks.contains(task_id.as_str()) {
        return Ok(None);
    }
    let runtime = bridge.runtime.as_ref().ok_or(())?;
    if runtime
        .procedure_catalogue()
        .recall()
        .iter()
        .any(|run| run.task_id == task_id.as_str())
    {
        return Ok(None);
    }
    let task = runtime.core().task(task_id).ok_or(())?;
    let Some(version_id) = core_runtime::builtin_skills::saved_procedure_version(
        task.builtin_skill_reference(),
        task.skill_version_id(),
    ) else {
        return Ok(None);
    };
    i64::try_from(now_utc_millis).map_err(|_| ())?;
    let procedure = runtime
        .procedure_catalogue()
        .resolve(version_id)
        .ok_or(())?;
    let view = task.view_facts().map_err(|_| ())?;
    let outcome = match view.state {
        core_runtime::TaskState::Completed => wire::SkillRunOutcome::Completed,
        core_runtime::TaskState::Partial | core_runtime::TaskState::Cancelled => {
            wire::SkillRunOutcome::Abandoned
        }
        core_runtime::TaskState::Failed => match view.terminal_failure {
            Some(
                core_runtime::FailureReason::ProviderUnavailable
                | core_runtime::FailureReason::JournalUnusable,
            ) => wire::SkillRunOutcome::Unavailable,
            Some(_) => wire::SkillRunOutcome::Refused,
            None => return Err(()),
        },
        _ => return Ok(None),
    };
    let deadline = now_monotonic_ms.checked_add(30_000).ok_or(())?;
    let material = format!(
        "taffy/skill-run/v1:{}:{}:{}:{}",
        task_id.as_str(),
        procedure.id.as_str(),
        procedure.version.0,
        view.revision
    );
    let digest: [u8; 32] = ffi::ChromiumSha256(material.as_bytes())
        .try_into()
        .map_err(|_| ())?;
    let mut hex = String::with_capacity(64);
    const NIBBLES: &[u8; 16] = b"0123456789abcdef";
    for byte in digest {
        hex.push(char::from(*NIBBLES.get(usize::from(byte >> 4)).ok_or(())?));
        hex.push(char::from(
            *NIBBLES.get(usize::from(byte & 0x0f)).ok_or(())?,
        ));
    }
    let operation_id = format!("skill-run-operation-{hex}");
    if bridge.pending_skill_runs.contains_key(&operation_id) {
        return Err(());
    }
    Ok(Some(PlannedSkillRun {
        operation_id: operation_id.clone(),
        task_id: task_id.as_str().to_owned(),
        effect: ffi::BridgeStorageEffect {
            operation: ffi::BridgeOperation {
                operation_id,
                service_generation: bridge.generation.value(),
                task_revision: view.revision,
                deadline_monotonic_ms: deadline,
                idempotency_key: format!("skill-run-idempotency-{hex}"),
            },
            effect_id: format!("skill-run-{hex}"),
            operation_kind: wire::StorageOperation::RecordSkillRun as u8,
            task_id: String::new(),
            expected_revision: 0,
            resulting_revision: 0,
            transaction_batch: Vec::new(),
            task_id_seed: [0; 32],
            workspace_id: String::new(),
            workspace_expected_revision: 0,
            workspace_resulting_revision: 0,
            workspace_snapshot: Vec::new(),
            skill_id: procedure.id.as_str().to_owned(),
            skill_version: procedure.version.0,
            skill_origin: String::new(),
            skill_provenance: 0,
            skill_status: 0,
            skill_definition: Vec::new(),
            skill_step_count: 0,
            skill_changed_at_utc_ms: 0,
            skill_task_id: task_id.as_str().to_owned(),
            skill_run_outcome: outcome as u8,
            skill_ran_at_utc_ms: now_utc_millis,
            configuration_disabled_abilities: Vec::new(),
            configuration_preset: 0,
            configuration_pace: 0,
            configuration_length: 0,
            configuration_check_in: 0,
        },
    }))
}

pub(crate) fn commit_skill_run_plan(bridge: &mut ServiceBridge, plan: &PlannedSkillRun) {
    bridge.pending_skill_runs.insert(
        plan.operation_id.clone(),
        PendingSkillRun {
            effect_id: plan.effect.effect_id.clone(),
            service_generation: plan.effect.operation.service_generation,
            task_revision: plan.effect.operation.task_revision,
            deadline_monotonic_ms: plan.effect.operation.deadline_monotonic_ms,
            idempotency_key: plan.effect.operation.idempotency_key.clone(),
            task_id: plan.task_id.clone(),
        },
    );
    bridge.recorded_skill_run_tasks.insert(plan.task_id.clone());
}
