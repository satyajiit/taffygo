// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A real handback is audited under its new lease, while recording keeps the
//! handover identity from the exact committed command.

use super::support;
use crate::contract::Completion;
use crate::procedure_catalogue::SkillMutationPersistence;
use task_engine::action::BrowserIntent;
use task_engine::{
    ActorLeaseId, BrowserSessionId, Command, EventKind, EventSubject, HandoverCompletion,
    HandoverId, PersonInput, TaskResult,
};

fn waiting_for_person() -> (crate::ProfileServiceRuntime, HandoverId) {
    let mut load = support::load();
    let crate::ports::TaskEngineLoad::Fresh { seed, .. } = &mut load else {
        panic!("recording starts from a fresh task")
    };
    seed.snapshot
        .tool_allowlist
        .push("browser.download.list".to_owned());
    let mut runtime = support::driver_for(load);
    support::action(
        &mut runtime,
        BrowserIntent::Navigate {
            tab: support::source().tab_id,
            address: "https://example.test/download-document".to_owned(),
            new_tab: false,
        },
        None,
    );
    let (before, _) = support::install_page(&mut runtime);
    support::action(
        &mut runtime,
        BrowserIntent::DomRead {
            tab: support::source().tab_id,
            target: None,
        },
        Some(before),
    );
    let handover = HandoverId::new("document-verification").unwrap();
    support::apply(
        &mut runtime,
        Command::RequestHandover {
            handover_id: handover.clone(),
        },
    );
    (runtime, handover)
}

pub(super) fn accepted_download_recording() -> (crate::ProfileServiceRuntime, String) {
    let (mut runtime, handover) = waiting_for_person();
    support::apply(
        &mut runtime,
        Command::CompleteHandover(
            HandoverCompletion::new(
                handover,
                ActorLeaseId::new("lease-before"),
                ActorLeaseId::new("lease-after"),
                PersonInput::observed(2),
            )
            .unwrap(),
        ),
    );
    let (fresh, link) = support::install_page(&mut runtime);
    support::action(
        &mut runtime,
        BrowserIntent::DomRead {
            tab: support::source().tab_id,
            target: None,
        },
        Some(fresh),
    );
    support::action(
        &mut runtime,
        BrowserIntent::DownloadFromLink {
            target: link,
            browser_session_id: BrowserSessionId::new("browser-session-1").unwrap(),
        },
        None,
    );
    support::action(
        &mut runtime,
        BrowserIntent::DownloadList {
            tab: support::source().tab_id,
            browser_session_id: BrowserSessionId::new("browser-session-1").unwrap(),
        },
        None,
    );
    support::apply(&mut runtime, Command::ResultCandidateReady);
    support::apply(
        &mut runtime,
        Command::CompleteResultValidated(TaskResult::default()),
    );
    let draft = runtime
        .prepare_completed_flow(&support::task_id(), 2_000)
        .unwrap();
    let id = draft.skill_id().to_owned();
    let version = format!("{id}@1");
    runtime.install_skill_mutation(draft).unwrap();
    let accepted = runtime
        .prepare_skill_mutation(&core_service_types::MutateSkillCommand {
            kind: core_service_types::SkillMutationKind::SetEnabled,
            skill_id: id,
            origin: String::new(),
            expected_version: 1,
            enabled: true,
            clauses: Vec::new(),
            steps: Vec::new(),
            admitted: 0,
            recorded_at_epoch_ms: 2_100,
        })
        .unwrap();
    runtime.install_skill_mutation(accepted).unwrap();
    (runtime, version)
}

pub(super) fn replay_start(version: String) -> core_service_types::StartTaskCommand {
    use core_service_types as wire;

    // The browser's NoModelRequired WebErrand shape, including its complete
    // reviewed bundle before the exact accepted recording narrows it.
    wire::StartTaskCommand {
        task_id: "replayed-download".to_owned(),
        workspace_id: None,
        browser_profile_id: "profile-1".to_owned(),
        browser_session_id: "browser-session-1".to_owned(),
        kind: wire::TaskKind::Errand,
        goal: "Download the public document".to_owned(),
        control_mode: wire::TaskControlMode::Assistant,
        provider_route_id: Some("no_model_required".to_owned()),
        assistant_config_version: 1,
        policy_version: 1,
        skill_version_id: Some(version),
        builtin_skill: None,
        tool_allowlist: [
            "browser.navigate",
            "browser.dom.read",
            "browser.dom.query",
            "browser.dom.click",
            "browser.dom.focus",
            "browser.link.open",
            "user.handover",
            "browser.download.list",
            "browser.download.from_link",
        ]
        .map(str::to_owned)
        .to_vec(),
        milestone: wire::TaskMilestone::M7,
        budgets: [
            (wire::TaskBudgetKind::MaxSources, 1),
            (wire::TaskBudgetKind::MaxModelRequests, 0),
            (wire::TaskBudgetKind::MaxInputUnits, 0),
            (wire::TaskBudgetKind::MaxOutputUnits, 0),
            (wire::TaskBudgetKind::MaxCostUnits, 0),
            (
                wire::TaskBudgetKind::MaxRetriesPerStep,
                crate::codec::start_shape::NO_MODEL_MAX_RETRIES_PER_STEP,
            ),
        ]
        .map(|(kind, limit)| wire::TaskBudget { kind, limit })
        .to_vec(),
        has_task_deadline: false,
        task_deadline_monotonic_ms: 0,
        task_deadline_utc_ms: 0,
        predecessor_task_id: None,
        trace_id: "replay-trace".to_owned(),
        task_id_seed: core::array::from_fn(|index| u8::try_from(index + 33).unwrap()),
        template_id: wire::TaskTemplateId::WebErrand,
        consent_preview: wire::TaskConsentPreview {
            sources: vec![wire::TaskConsentSource {
                source_id: support::source().source_id.to_text(),
                tab_id: support::source().tab_id.as_str().to_owned(),
                normalized_origin: support::source().normalized_origin,
                canonical_locator: Some("https://example.test/download-document".to_owned()),
            }],
            source_discovery_enabled: false,
            new_source_cap: 0,
            provider_route: wire::TaskProviderRoute::NoModelRequired,
        },
        initial_consent_receipt_id: "replay-consent".to_owned(),
        library_refresh: None,
    }
}

#[test]
fn recorded_download_prepares_decodes_and_commits_a_model_free_task_start() {
    use crate::contract::{Deadline, OperationId};

    let (mut runtime, version) = accepted_download_recording();
    let procedure = runtime.procedure_catalogue().resolve(&version).unwrap();
    assert!(procedure.is_runnable());
    assert_eq!(
        procedure
            .steps
            .iter()
            .map(|step| step.verb.as_str())
            .collect::<Vec<_>>(),
        [
            "browser.navigate",
            "browser.dom.read",
            "user.handover",
            "browser.dom.read",
            "browser.download.from_link",
            "browser.download.list",
        ]
    );
    let mut start = replay_start(version.clone());
    runtime.prepare_skill_start(&mut start).unwrap();
    let operation = core_service_types::OperationEnvelope {
        operation_id: "replay-open".to_owned(),
        service_generation: 1,
        task_revision: 0,
        deadline_monotonic_ms: 10_000,
        idempotency_key: "replay-create".to_owned(),
    };
    let load = crate::decode_start_task(&start, &operation).unwrap();
    let task_id = bip_types::identity::TaskId::new(start.task_id);
    let opened = runtime
        .core_mut()
        .begin_open_task(
            load,
            OperationId::new(operation.operation_id).unwrap(),
            Deadline::from_millis(operation.deadline_monotonic_ms),
            1,
            2_200,
        )
        .unwrap();
    assert!(runtime.core().task(&task_id).is_none());
    let wrong = opened.commit.with_body(Completion::StorageCommitted {
        committed_revision: opened.commit.task_revision + 1,
    });
    assert!(runtime
        .core_mut()
        .complete_open_task(&task_id, wrong, 2)
        .is_err());
    assert!(runtime.core().task(&task_id).is_none());
    let committed = opened.commit.with_body(Completion::StorageCommitted {
        committed_revision: opened.commit.task_revision,
    });
    runtime
        .core_mut()
        .complete_open_task(&task_id, committed, 2)
        .unwrap();
    let task = runtime.core().task(&task_id).unwrap();
    assert_eq!(task.revision(), opened.commit.task_revision);
    assert_eq!(task.skill_version_id(), Some(version.as_str()));
    assert_eq!(
        task.model_turn_facts().provider_route_id.unwrap(),
        "no_model_required"
    );
    assert_eq!(task.pre_model_observation_sources().len(), 1);
}

#[test]
fn an_exact_committed_handback_completes_the_recorded_person_step() {
    let (mut runtime, handover) = waiting_for_person();
    let pending = support::begin(
        &mut runtime,
        Command::CompleteHandover(
            HandoverCompletion::new(
                handover,
                ActorLeaseId::new("lease-before"),
                ActorLeaseId::new("lease-after"),
                PersonInput::observed(2),
            )
            .unwrap(),
        ),
    );
    let recorded = |runtime: &crate::ProfileServiceRuntime| {
        runtime
            .core()
            .loop_state(support::task_id().as_str())
            .unwrap()
            .recording
            .finish(
                procedure_engine::ProcedureId::new("probe-flow").unwrap(),
                support::task_id().as_str(),
            )
    };
    assert!(recorded(&runtime).is_none());
    let wrong = pending.with_body(Completion::StorageCommitted {
        committed_revision: pending.task_revision + 1,
    });
    assert!(runtime
        .core_mut()
        .complete_commit(&support::task_id(), wrong, 2)
        .is_err());
    assert!(recorded(&runtime).is_none());
    let committed = support::commit(&mut runtime, &pending);
    assert!(committed.events.iter().any(|event| {
        event.kind == EventKind::HandoverCompleted
            && event.subject == Some(EventSubject::ActorLease(ActorLeaseId::new("lease-after")))
    }));
    assert!(recorded(&runtime).is_some());

    let (fresh, link) = support::install_page(&mut runtime);
    support::action(
        &mut runtime,
        BrowserIntent::DomRead {
            tab: support::source().tab_id,
            target: None,
        },
        Some(fresh),
    );
    support::action(
        &mut runtime,
        BrowserIntent::DownloadFromLink {
            target: link,
            browser_session_id: BrowserSessionId::new("browser-session-1").unwrap(),
        },
        None,
    );
    support::apply(&mut runtime, Command::ResultCandidateReady);
    support::apply(
        &mut runtime,
        Command::CompleteResultValidated(TaskResult::default()),
    );
    let draft = runtime
        .prepare_completed_flow(&support::task_id(), 2_000)
        .unwrap();
    let SkillMutationPersistence::Install(install) = draft.persistence() else {
        panic!("a completed person step must produce a reviewable draft")
    };
    let procedure =
        procedure_engine::decode_beside(&install.definition, install.step_count).unwrap();
    assert_eq!(
        procedure
            .steps
            .iter()
            .map(|step| step.verb.as_str())
            .collect::<Vec<_>>(),
        [
            "browser.navigate",
            "browser.dom.read",
            "user.handover",
            "browser.dom.read",
            "browser.download.from_link"
        ]
    );
}
