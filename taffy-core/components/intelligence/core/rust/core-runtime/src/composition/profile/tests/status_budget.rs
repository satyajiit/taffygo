// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_service_types as wire;

use super::built_runtime;

#[test]
fn oversized_durable_library_uses_recovery_without_poisoning_runtime() {
    let mut runtime = built_runtime();
    let entries: Vec<_> = (0..17).map(large_entry).collect();
    assert!(runtime
        .core_mut()
        .restore_library(17, entries.clone())
        .is_ok());

    let complete = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap();
    assert_eq!(
        complete.projection_mode,
        core_api_types::CoreStatusProjectionMode::Complete
    );
    assert_eq!(complete.library.entries.len(), entries.len());
    assert_eq!(
        core_api_types::measure_core_status_payload(&complete),
        Err(core_api_types::CoreStatusPayloadCodecError::SizeLimit)
    );

    let encoded = runtime.encode_ready_core_status().unwrap();
    assert!(encoded.payload.len() <= core_api_types::MAX_EVENT_PAYLOAD_BYTES);
    let recovered = core_api_types::decode_core_status_payload(&encoded.payload).unwrap();
    assert_eq!(
        recovered.projection_mode,
        core_api_types::CoreStatusProjectionMode::RecoveryRequired
    );
    assert_eq!(recovered.projection_omissions.len(), 13);
    assert_eq!(recovered.builtin_skills.len(), 16);
    assert!(recovered.library.entries.is_empty());
    assert_eq!(
        recovered.library.availability,
        core_api_types::LibraryAvailability::Unavailable
    );
    let library = recovered
        .projection_omissions
        .iter()
        .find(|omission| omission.family == core_api_types::CoreStatusProjectionFamily::Library)
        .unwrap();
    assert_eq!((library.revision, library.item_count), (17, 17));

    let still_complete = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap();
    assert_eq!(still_complete.library.entries.len(), entries.len());
    assert_eq!(
        still_complete.library.entries[0].original_value.len(),
        16_384
    );

    assert!(runtime
        .core_mut()
        .restore_library(18, vec![small_entry()])
        .is_ok());
    let next = core_api_types::decode_core_status_payload(
        &runtime.encode_ready_core_status().unwrap().payload,
    )
    .unwrap();
    assert_eq!(
        next.projection_mode,
        core_api_types::CoreStatusProjectionMode::Complete
    );
    assert_eq!(next.library.revision, 18);
    assert_eq!(next.library.entries.len(), 1);
}

#[test]
fn many_valid_recorded_flows_keep_metadata_and_only_whole_reviews_in_status() {
    let mut runtime = built_runtime();
    for index in 0..wire::MAX_SKILLS_PER_PROFILE {
        let prepared = runtime
            .prepare_recorded_procedure(long_address_flow(index), 1_000)
            .unwrap();
        runtime.install_skill_mutation(prepared).unwrap();
    }
    let full = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap();
    assert_eq!(
        core_api_types::measure_core_status_payload(&full),
        Err(core_api_types::CoreStatusPayloadCodecError::SizeLimit)
    );
    let encoded = runtime.encode_ready_core_status().unwrap();
    let bounded = core_api_types::decode_core_status_payload(&encoded.payload).unwrap();
    assert_eq!(
        bounded.projection_mode,
        core_api_types::CoreStatusProjectionMode::Complete
    );
    assert_eq!(bounded.provider_roster, full.provider_roster);
    assert_eq!(bounded.site_skills.len(), wire::MAX_SKILLS_PER_PROFILE);
    let kept = bounded
        .site_skills
        .iter()
        .take_while(|skill| !skill.reviewed_steps.is_empty())
        .count();
    assert!(kept > 0 && kept < bounded.site_skills.len());
    for (index, (actual, original)) in bounded
        .site_skills
        .iter()
        .zip(&full.site_skills)
        .enumerate()
    {
        let mut expected = original.clone();
        if index >= kept {
            expected.reviewed_steps.clear();
        }
        assert_eq!(actual, &expected);
    }
    let mut one_more = bounded.clone();
    one_more.site_skills[kept].reviewed_steps = full.site_skills[kept].reviewed_steps.clone();
    assert_eq!(
        core_api_types::measure_core_status_payload(&one_more),
        Err(core_api_types::CoreStatusPayloadCodecError::SizeLimit)
    );
    // Encoding has not shortened or rewritten any stored definition.
    assert_eq!(
        runtime.procedure_catalogue().site_skill_views(),
        full.site_skills
    );
    assert_eq!(
        runtime.encode_ready_core_status().unwrap().payload,
        encoded.payload
    );
    // Fetching one exact definition bypasses only the aggregate projection
    // budget. It neither truncates the review nor alters the published state.
    let omitted = &bounded.site_skills[kept];
    let request = wire::SavedFlowQueryCommand {
        operation: wire::OperationEnvelope {
            operation_id: "review".to_owned(),
            service_generation: 1,
            task_revision: 0,
            deadline_monotonic_ms: 100,
            idempotency_key: "review".to_owned(),
        },
        kind: wire::SavedFlowQueryKind::Review,
        goal: String::new(),
        skill_id: omitted.skill_id.clone(),
        expected_version: omitted.active_version,
    };
    let (status, reviews) = runtime.query_saved_flows(&request);
    assert_eq!(status, wire::SavedFlowQueryStatus::Available);
    assert_eq!(reviews.len(), 1);
    assert_eq!(
        reviews[0].reviewed_steps.len(),
        full.site_skills[kept].reviewed_steps.len()
    );
    assert_eq!(
        reviews[0].reviewed_steps[0].arguments[0].public_address,
        full.site_skills[kept].reviewed_steps[0].arguments[0].public_address
    );
    assert_eq!(
        runtime.encode_ready_core_status().unwrap().payload,
        encoded.payload
    );
}

fn long_address_flow(index: usize) -> procedure_engine::Procedure {
    use bip_types::{action::PostconditionKind, snapshot::SemanticRole};
    use procedure_engine::{
        MatchClause, MatchCondition, PhraseId, Procedure, ProcedureId, ProcedureProvenance,
        ProcedureScope, ProcedureStep, StepArgument, StepValue,
    };
    use task_engine::tool::ArgumentValue;

    let origin = "https://documents.example";
    let address = format!(
        "{origin}/{}",
        "x".repeat(task_engine::MAX_ARGUMENT_VALUE_BYTES - origin.len() - 1)
    );
    Procedure::draft(
        ProcedureId::new(format!("recorded.{index:02}")).unwrap(),
        ProcedureScope::for_origin(origin).unwrap(),
        MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::Document)]),
        vec![
            ProcedureStep::new("browser.navigate", PostconditionKind::CommittedNavigation).taking(
                vec![StepArgument::literal(
                    "address",
                    ArgumentValue::Address(address),
                )],
            ),
            ProcedureStep::new("browser.dom.read", PostconditionKind::NoMutation),
            ProcedureStep::new(
                "browser.download.from_link",
                PostconditionKind::BrowserFlowStarted,
            )
            .taking(vec![StepArgument::new(
                "node",
                StepValue::SemanticTarget {
                    role: SemanticRole::Link,
                    phrase: PhraseId::Download,
                },
            )]),
        ],
        ProcedureProvenance::RecordedFromTask,
    )
    .from_task(format!("completed-{index}"))
    .unwrap()
}

fn large_entry(index: usize) -> wire::LibraryEntryRecord {
    entry(index, "x".repeat(16_384))
}

fn small_entry() -> wire::LibraryEntryRecord {
    entry(1, "small durable value".to_owned())
}

fn entry(index: usize, original_value: String) -> wire::LibraryEntryRecord {
    let suffix = index + 1;
    wire::LibraryEntryRecord {
        entry_id: format!("{suffix:032x}"),
        revision: 1,
        collection_id: format!("{:032x}", 2_000 + suffix),
        collection_name: "Durable collection".to_owned(),
        source_workspace_id: format!("{:032x}", 3_000 + suffix),
        source_workspace_revision: 1,
        source_fact_id: format!("{:032x}", 4_000 + suffix),
        field: "durable field".to_owned(),
        original_value,
        correction: None,
        kind: wire::LibraryFactKind::FromPage,
        sources: vec![wire::LibrarySourceRecord {
            source_id: format!("{:032x}", 5_000 + suffix),
            title: "Durable source".to_owned(),
            host: "source.example".to_owned(),
            observed_at_epoch_ms: 1,
        }],
        captured_at_epoch_ms: 1,
        last_checked_epoch_ms: 1,
        has_conflict: false,
    }
}
