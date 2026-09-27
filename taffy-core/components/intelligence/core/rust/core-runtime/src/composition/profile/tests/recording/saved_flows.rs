// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Real completed task associations, with only storage acknowledgements simulated.

use super::support;
use crate::procedure_catalogue::ProcedureCatalogue;
use crate::ProfileServiceRuntime;
use core_service_types as wire;
use procedure_engine::ProcedureStatus;
use task_engine::{Command, Milestone, TaskResult};

fn recorded() -> (ProfileServiceRuntime, String) {
    let mut runtime = support::driver();
    support::perform_flow(&mut runtime);
    support::apply(&mut runtime, Command::ResultCandidateReady);
    support::apply(
        &mut runtime,
        Command::CompleteResultValidated(TaskResult::default()),
    );
    let draft = runtime
        .prepare_completed_flow(&support::task_id(), 2_000)
        .unwrap();
    let id = draft.skill_id().to_owned();
    runtime.install_skill_mutation(draft).unwrap();
    (runtime, id)
}

fn enable(runtime: &mut ProfileServiceRuntime, id: &str, enabled: bool) {
    let change = runtime
        .prepare_skill_mutation(&wire::MutateSkillCommand {
            kind: wire::SkillMutationKind::SetEnabled,
            skill_id: id.to_owned(),
            expected_version: 1,
            origin: String::new(),
            clauses: Vec::new(),
            steps: Vec::new(),
            admitted: 0,
            enabled,
            recorded_at_epoch_ms: 2_100,
        })
        .unwrap();
    runtime.install_skill_mutation(change).unwrap();
}

fn query(
    kind: wire::SavedFlowQueryKind,
    goal: &str,
    id: &str,
    version: u32,
) -> wire::SavedFlowQueryCommand {
    wire::SavedFlowQueryCommand {
        operation: wire::OperationEnvelope {
            operation_id: "query".to_owned(),
            service_generation: 1,
            task_revision: 0,
            deadline_monotonic_ms: 100,
            idempotency_key: "query".to_owned(),
        },
        kind,
        goal: goal.to_owned(),
        skill_id: id.to_owned(),
        expected_version: version,
    }
}

#[test]
fn exact_repeat_requires_saved_acceptance_and_preserves_complete_review() {
    let (mut runtime, id) = recorded();
    let request = query(
        wire::SavedFlowQueryKind::ExactGoal,
        "\u{2003}Download the public document\n",
        "",
        0,
    );
    assert!(runtime.query_saved_flows(&request).1.is_empty());
    let review = query(wire::SavedFlowQueryKind::Review, "", &id, 1);
    let before = runtime.query_saved_flows(&review).1;
    assert_eq!(before.len(), 1);
    assert_eq!(before[0].status, wire::SkillStatus::Draft);
    enable(&mut runtime, &id, true);
    let (status, found) = runtime.query_saved_flows(&request);
    assert_eq!(status, wire::SavedFlowQueryStatus::Available);
    assert_eq!(found.len(), 1);
    assert_eq!(found[0].skill_id, id);
    assert_eq!(found[0].reviewed_steps, before[0].reviewed_steps);
    assert_eq!(found[0].reviewed_steps.len(), found[0].step_count as usize);
    assert_eq!(found[0].reviewed_steps[0].verb, "browser.navigate");
    for changed in [
        "download the public document",
        "Download  the public document",
        "Download the public document.",
        "Get the public document",
    ] {
        assert!(runtime
            .query_saved_flows(&query(wire::SavedFlowQueryKind::ExactGoal, changed, "", 0))
            .1
            .is_empty());
    }
}

#[test]
fn disabled_and_stale_versions_cannot_supply_a_start_but_can_be_reviewed() {
    let (mut runtime, id) = recorded();
    enable(&mut runtime, &id, true);
    assert_eq!(
        runtime
            .query_saved_flows(&query(wire::SavedFlowQueryKind::PublicStart, "", &id, 1))
            .1
            .len(),
        1
    );
    enable(&mut runtime, &id, false);
    assert!(runtime
        .query_saved_flows(&query(wire::SavedFlowQueryKind::PublicStart, "", &id, 1))
        .1
        .is_empty());
    assert!(runtime
        .query_saved_flows(&query(
            wire::SavedFlowQueryKind::ExactGoal,
            "Download the public document",
            "",
            0
        ))
        .1
        .is_empty());
    assert_eq!(
        runtime
            .query_saved_flows(&query(wire::SavedFlowQueryKind::Review, "", &id, 1))
            .1
            .len(),
        1
    );
    assert!(runtime
        .query_saved_flows(&query(wire::SavedFlowQueryKind::Review, "", &id, 2))
        .1
        .is_empty());
}

#[test]
fn a_missing_retained_source_task_or_superseded_definition_is_not_a_candidate() {
    let (mut runtime, id) = recorded();
    enable(&mut runtime, &id, true);
    let original = runtime
        .procedures
        .resolve(&format!("{id}@1"))
        .unwrap()
        .clone();
    let request = query(
        wire::SavedFlowQueryKind::ExactGoal,
        "Download the public document",
        "",
        0,
    );
    for (source, status, projected_status) in [
        (
            "absent-task",
            ProcedureStatus::Active,
            wire::SkillStatus::Active,
        ),
        (
            support::task_id().as_str(),
            ProcedureStatus::Superseded,
            wire::SkillStatus::Superseded,
        ),
    ] {
        let mut procedure = original.clone();
        procedure.recorded_from_task_id = Some(source.to_owned());
        procedure.status = status;
        let record = wire::SkillRecord {
            skill_id: id.clone(),
            origin: "https://example.test".to_owned(),
            provenance: wire::SkillProvenance::RecordedFromTask,
            status: projected_status,
            active_version: 1,
            definition: procedure_engine::encode(&procedure).unwrap(),
            step_count: u32::try_from(procedure.steps.len()).expect("bounded recorded steps"),
            installed_at_utc_ms: 2_000,
            updated_at_utc_ms: 2_100,
        };
        runtime.procedures =
            ProcedureCatalogue::restore(vec![record], Vec::new(), Milestone::M8).unwrap();
        assert!(runtime.query_saved_flows(&request).1.is_empty());
    }
}

#[test]
fn private_profile_and_invalid_shapes_return_no_content() {
    let (mut runtime, id) = recorded();
    enable(&mut runtime, &id, true);
    for request in [
        query(wire::SavedFlowQueryKind::ExactGoal, "", "", 0),
        query(
            wire::SavedFlowQueryKind::ExactGoal,
            "Download the public document",
            &id,
            1,
        ),
        query(wire::SavedFlowQueryKind::Review, "private input", &id, 1),
        query(wire::SavedFlowQueryKind::Review, "", &id, 0),
    ] {
        assert_eq!(
            runtime.query_saved_flows(&request),
            (wire::SavedFlowQueryStatus::InvalidRequest, Vec::new())
        );
    }
    runtime.private_profile = true;
    for request in [
        query(
            wire::SavedFlowQueryKind::ExactGoal,
            "Download the public document",
            "",
            0,
        ),
        query(wire::SavedFlowQueryKind::Review, "", &id, 1),
        query(wire::SavedFlowQueryKind::PublicStart, "", &id, 1),
    ] {
        assert_eq!(
            runtime.query_saved_flows(&request),
            (wire::SavedFlowQueryStatus::PrivateProfile, Vec::new())
        );
    }
}
