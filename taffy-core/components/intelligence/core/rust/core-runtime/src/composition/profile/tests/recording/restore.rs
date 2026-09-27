// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Completed task history is independent of current saved-flow availability.

use super::{handover, support};
use crate::ports::TaskEngineLoad;
use crate::procedure_catalogue::{ProcedureCatalogue, SkillStartError};
use core_service_types as wire;
use procedure_engine::{Procedure, ProcedureStatus, ProcedureVersion};
use task_engine::{Command, Milestone, PauseCause, TaskResult, TaskState};

fn journal_at(state: TaskState) -> (TaskEngineLoad, Procedure, String) {
    let (catalogue, version) = handover::accepted_download_recording();
    let procedure = catalogue
        .procedure_catalogue()
        .resolve(&version)
        .unwrap()
        .clone();
    let mut start = handover::replay_start(version.clone());
    start.task_id = support::task_id().as_str().to_owned();
    catalogue.prepare_skill_start(&mut start).unwrap();
    let load = crate::decode_start_task(
        &start,
        &wire::OperationEnvelope {
            operation_id: "history-open".to_owned(),
            service_generation: 1,
            task_revision: 0,
            deadline_monotonic_ms: 10_000,
            idempotency_key: "history-create".to_owned(),
        },
    )
    .unwrap();
    let TaskEngineLoad::Fresh {
        seed, id_entropy, ..
    } = &load
    else {
        unreachable!()
    };
    let (mut seed, entropy) = (*seed.clone(), id_entropy.clone());
    let mut running = support::driver_for(load);
    seed.workspace_id = running
        .core()
        .task(&support::task_id())
        .unwrap()
        .workspace_id()
        .copied();
    match state {
        TaskState::Completed => {
            support::apply(&mut running, Command::ResultCandidateReady);
            support::apply(
                &mut running,
                Command::CompleteResultValidated(TaskResult::default()),
            );
        }
        TaskState::Paused => {
            support::apply(
                &mut running,
                Command::PauseTask {
                    cause: PauseCause::User,
                },
            );
            support::apply(&mut running, Command::PauseSettled);
        }
        TaskState::Running => {}
        _ => unreachable!(),
    }
    let task = running.core().task(&support::task_id()).unwrap();
    assert_eq!(task.view_facts().unwrap().state, state);
    (
        TaskEngineLoad::replay(seed, task.journal().clone(), entropy),
        procedure,
        version,
    )
}

fn current_catalogue(mut procedure: Procedure, replaced: bool) -> ProcedureCatalogue {
    let status = if replaced {
        procedure.version = ProcedureVersion(procedure.version.0 + 1);
        procedure.status = ProcedureStatus::Active;
        wire::SkillStatus::Active
    } else {
        procedure.status = ProcedureStatus::Disabled;
        wire::SkillStatus::Disabled
    };
    ProcedureCatalogue::restore(
        vec![wire::SkillRecord {
            skill_id: procedure.id.as_str().to_owned(),
            origin: procedure.scope.origin().display(),
            provenance: wire::SkillProvenance::RecordedFromTask,
            status,
            active_version: procedure.version.0,
            definition: procedure_engine::encode(&procedure).unwrap(),
            step_count: u32::try_from(procedure.steps.len()).unwrap(),
            installed_at_utc_ms: 2_000,
            updated_at_utc_ms: 3_000,
        }],
        Vec::new(),
        Milestone::M8,
    )
    .unwrap()
}

#[test]
fn completed_recorded_task_restores_after_its_flow_is_disabled_or_replaced() {
    for replaced in [false, true] {
        let (load, procedure, version) = journal_at(TaskState::Completed);
        let mut restarted = super::super::built_runtime();
        restarted.procedures = current_catalogue(procedure, replaced);
        let restored = restarted.core_mut().restore_task(load).unwrap();
        assert!(restored.effects.is_empty());
        let recovery = restored.recovery.unwrap();
        assert_eq!(recovery.state, TaskState::Completed);
        assert!(!recovery.requires_revalidation);
        assert_eq!(recovery.leases_restored, 0);
        assert!(recovery.unknown_outcome_actions.is_empty());
        assert_eq!(
            restarted.validate_restored_procedure_task(&support::task_id()),
            Ok(())
        );
        restarted
            .validate_restored_builtin_task(&support::task_id())
            .unwrap();

        let status = restarted.encode_ready_core_status().unwrap();
        let status = core_api_types::decode_core_status_payload(&status.payload).unwrap();
        assert_eq!(status.availability, core_api_types::CoreAvailability::Ready);
        let task = restarted.core().task(&support::task_id()).unwrap();
        assert_eq!(task.view_facts().unwrap().state, TaskState::Completed);
        assert_eq!(task.skill_version_id(), Some(version.as_str()));
        assert!(restarted
            .prepare_skill_start(&mut handover::replay_start(version))
            .is_err());

        let revision = task.revision();
        let attempt = restarted.core_mut().begin_submit(
            &support::task_id(),
            task_engine::CommandEnvelope::new(
                task_engine::IdempotencyKey::new("cannot-resume-completed"),
                revision,
                task_engine::TraceId::new("cannot-resume-completed"),
                Command::ResumeTask,
            ),
            crate::contract::OperationId::new("cannot-resume-completed").unwrap(),
            crate::contract::Deadline::from_millis(10_000),
            1,
            4_000,
        );
        assert!(attempt.is_err());
        assert_eq!(
            restarted
                .core()
                .task(&support::task_id())
                .unwrap()
                .revision(),
            revision
        );
    }
}

#[test]
fn unfinished_recorded_tasks_still_require_the_exact_runnable_flow_on_restore() {
    for state in [TaskState::Running, TaskState::Paused] {
        for replaced in [false, true] {
            let (load, procedure, _) = journal_at(state);
            let mut restarted = super::super::built_runtime();
            restarted.procedures = current_catalogue(procedure, replaced);
            restarted.core_mut().restore_task(load).unwrap();
            assert_eq!(
                restarted.validate_restored_procedure_task(&support::task_id()),
                Err(if replaced {
                    SkillStartError::UnknownVersion
                } else {
                    SkillStartError::NotRunnable
                })
            );
        }
    }
}

/// A task that came back from the journal already over is not this run's news,
/// and the surfaces are not told about it (decision 0149).
///
/// The bar of a freshly started browser showed "Couldn't finish — Taffy could
/// not read enough to answer" for a task that had ended the day before, and it
/// kept showing it: every task ever committed is restored, nothing distinguished
/// them, and the runtime holds its sessions keyed by identity, so the "current"
/// task was whichever identity sorted first for the life of the profile.
#[test]
fn a_task_restored_already_over_is_not_on_any_surface() {
    let (load, procedure, _) = journal_at(TaskState::Completed);
    let mut restarted = super::super::built_runtime();
    restarted.procedures = current_catalogue(procedure, false);
    restarted.core_mut().restore_task(load).unwrap();

    let encoded = restarted.encode_ready_core_status().unwrap();
    let status = core_api_types::decode_core_status_payload(&encoded.payload).unwrap();
    assert!(
        status.active_tasks.is_empty(),
        "a task that ended before this run leads no surface: {:?}",
        status
            .active_tasks
            .iter()
            .map(|task| task.task_id.clone())
            .collect::<Vec<_>>()
    );
}

/// And the browser still knows what it is, which is the other half of the same
/// rule and the half the first attempt at it broke.
///
/// The browser routes a restored task effect by looking the task up in
/// `task_revisions`. Filtering the ended task out one step earlier — before the
/// bindings are built rather than after — left that map without it, and the
/// phone answered `[taffy_core_state_projection_refused]
/// at=task-effect/unknown-task` and then refused to start the core at all.
#[test]
fn a_task_restored_already_over_still_binds_for_the_browser() {
    let (load, procedure, _) = journal_at(TaskState::Completed);
    let mut restarted = super::super::built_runtime();
    restarted.procedures = current_catalogue(procedure, false);
    restarted.core_mut().restore_task(load).unwrap();

    let encoded = restarted.encode_ready_core_status().unwrap();
    assert!(
        encoded
            .task_revisions
            .iter()
            .any(|binding| binding.task_id == support::task_id().as_str()),
        "the ended task keeps its browser binding",
    );
}

/// A task restored still going is this run's own, and leads.
///
/// `terminal_on_restore` is fixed at the moment of restore rather than read
/// live, which is what makes this stable: a task that came back paused and then
/// finished during this run had its say here, and keeps it.
#[test]
fn a_task_restored_still_going_is_on_the_surfaces() {
    let (load, procedure, _) = journal_at(TaskState::Paused);
    let mut restarted = super::super::built_runtime();
    restarted.procedures = current_catalogue(procedure, false);
    restarted.core_mut().restore_task(load).unwrap();

    let encoded = restarted.encode_ready_core_status().unwrap();
    let status = core_api_types::decode_core_status_payload(&encoded.payload).unwrap();
    assert_eq!(status.active_tasks.len(), 1);
    assert_eq!(status.active_tasks[0].task_id, support::task_id().as_str());
}
