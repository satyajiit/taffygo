// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Restoring every committed task at bootstrap, and naming the first that
//! does not restore.
//!
//! Its own module because it is the part of bootstrap with a policy in it:
//! which task refused, why, and which commands a replay held as history
//! (decision 0235). The rest of [`super`] turns browser records into
//! runtime state and refuses as one.

use core_runtime::{decode_task_restore, wire, ProfileServiceRuntime};

use crate::service_bridge_bootstrap_ffi::ffi as bootstrap_ffi;
use crate::service_bridge_task_effect::RestoredTaskEffects;
use crate::service_bridge_trace as trace;

/// Restores every committed task, or names the first refusal.
///
/// One refused task still refuses the core. The name is what changed: the
/// label rides beside `reason=task-restore` and a trace line says which task,
/// where before both were empty (decision 0235).
pub(super) fn restore_tasks(
    runtime: &mut ProfileServiceRuntime,
    tasks: Vec<bootstrap_ffi::BridgeTaskRestore>,
    restored_effects: &mut Vec<RestoredTaskEffects>,
) -> Result<(), &'static str> {
    for task in tasks {
        let record = wire::TaskRestoreRecord {
            task_id: task.task_id,
            batches: task
                .batches
                .into_iter()
                .map(|batch| wire::CommittedTaskBatch {
                    effect_id: batch.effect_id,
                    expected_revision: batch.expected_revision,
                    resulting_revision: batch.resulting_revision,
                    transaction_batch: batch.transaction_batch,
                })
                .collect(),
            task_id_seed: task.task_id_seed,
        };
        restore_task(runtime, &record, restored_effects).map_err(|label| {
            trace::restore_refused(&record.task_id, label);
            label
        })?;
    }
    Ok(())
}

fn restore_task(
    runtime: &mut ProfileServiceRuntime,
    record: &wire::TaskRestoreRecord,
    restored_effects: &mut Vec<RestoredTaskEffects>,
) -> Result<(), &'static str> {
    let Ok(decoded) = decode_task_restore(record) else {
        return Err(runtime
            .task_restore_refusal(record)
            .unwrap_or("restore_task_decode"));
    };
    let task_id = decoded.load.task_id().clone();
    let opened = runtime.core_mut().restore_task(decoded.load);
    let outcome = match opened {
        Ok(outcome) => outcome,
        Err(error) => {
            return Err(runtime
                .task_restore_refusal(record)
                .unwrap_or_else(|| error.label()))
        }
    };
    if runtime.validate_restored_builtin_task(&task_id).is_err() {
        return Err("restore_task_builtin_skill");
    }
    if runtime.validate_restored_procedure_task(&task_id).is_err() {
        return Err("restore_task_procedure");
    }
    if let Some(recovery) = outcome.recovery.as_ref() {
        trace::restored_over_history(&task_id, &recovery.admitted_as_history);
    }
    let Some(batch) = decoded.unresolved_effects else {
        return Ok(());
    };
    if batch.task_revision != outcome.revision {
        return Err("restore_task_effect_revision");
    }
    let Some(task) = runtime.core().task(&task_id) else {
        return Err("restore_task_missing");
    };
    let mut effects = Vec::with_capacity(batch.effects.len());
    for effect in batch.effects {
        match effect {
            core_runtime::Effect::DispatchAction { action_id } => {
                let Some(facts) = task.action_effect_facts(&action_id) else {
                    return Err("restore_task_action_facts");
                };
                if !outcome
                    .recovery
                    .as_ref()
                    .is_some_and(|recovery| recovery.unknown_outcome_actions.contains(&action_id))
                {
                    return Err("restore_task_action_not_unknown");
                }
                effects.push(core_runtime::Effect::ReconcileAction {
                    action_id,
                    rule: facts.proposal.idempotency().recovery_rule(),
                });
            }
            other => effects.push(other),
        }
    }
    if !effects.is_empty() {
        restored_effects.push(RestoredTaskEffects {
            task_id,
            parent_operation_id: batch.parent_operation_id,
            task_revision: batch.task_revision,
            effects,
        });
    }
    Ok(())
}
