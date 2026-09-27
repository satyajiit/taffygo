// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Accepted records carry public navigation and closed targets, never input values.

#![allow(clippy::expect_used, clippy::panic)]

use bip_types::{action::PostconditionKind, snapshot::SemanticRole};
use procedure_engine::{ArgumentDescriptor as Arg, RecordedValue as Value, *};
use task_engine::Milestone;

fn recording(address: &str) -> Recording {
    let origin =
        policy_engine::origin::normalize_serialization("https://example.test").expect("origin");
    let entries = vec![
        LedgerEntry::Described(
            StepDescriptor::new("browser.navigate", PostconditionKind::CommittedNavigation)
                .taking(vec![Arg::new(0, Value::PublicAddress(address.to_owned()))]),
        ),
        LedgerEntry::Described(StepDescriptor::new(
            "browser.dom.read",
            PostconditionKind::NoMutation,
        )),
        LedgerEntry::Described(
            StepDescriptor::new("user.handover", PostconditionKind::NoMutation)
                .taking(vec![Arg::new(0, Value::Choice { index: 1 })]),
        ),
        LedgerEntry::Described(
            StepDescriptor::new(
                "browser.download.from_link",
                PostconditionKind::BrowserFlowStarted,
            )
            .taking(vec![Arg::new(
                0,
                Value::SemanticTarget {
                    role: SemanticRole::Link,
                    phrase: PhraseId::Download,
                },
            )]),
        ),
    ];
    Recording::new(
        origin,
        vec![MatchClause::RolePresent(SemanticRole::Document)],
        entries,
        4,
    )
}

#[test]
fn recorded_flow_round_trips_the_task_address_handover_and_semantic_target() {
    let procedure = record_procedure(
        ProcedureId::new("learned.download").expect("id"),
        &recording("https://example.test/documents"),
        Milestone::M8,
    )
    .expect("recorded descriptors")
    .from_task("task-completed-1")
    .expect("internal task association");
    assert_eq!(procedure.status, ProcedureStatus::Draft);
    assert_eq!(
        procedure.recorded_from_task_id.as_deref(),
        Some("task-completed-1")
    );
    assert_eq!(procedure.steps.len(), 4);
    let bytes = encode(&procedure).expect("stored definition");
    assert_eq!(decode(&bytes), Ok(procedure.clone()));
    assert!(narrowing::required_verbs(&procedure, Milestone::M8)
        .iter()
        .any(|verb| verb == "browser.download.list"));
    let mut active = procedure.clone();
    active.status = transition(
        active.status,
        ProcedureStatus::Active,
        LifecycleActor::Person,
    )
    .expect("person accepts");
    assert!(active.is_runnable());
    assert!(!procedure.is_runnable());
}

#[test]
fn transient_or_cross_origin_start_addresses_refuse_the_whole_record() {
    for address in [
        "https://example.test/doc?token=temporary",
        "https://example.test/doc#private",
        "https://user@example.test/doc",
        "http://example.test/doc",
        "https://other.test/doc",
        "https://example.test.evil/doc",
        "https://example.test/line\nbreak",
    ] {
        assert_eq!(
            record_procedure(
                ProcedureId::new("learned.download").expect("id"),
                &recording(address),
                Milestone::M8
            ),
            Err(SkillRecordError::InvalidPublicAddress)
        );
    }
}

#[test]
fn unsupported_intermediate_steps_prevent_automatic_task_association() {
    let mut procedure = record_procedure(
        ProcedureId::new("learned.download").expect("id"),
        &recording("https://example.test/doc"),
        Milestone::M8,
    )
    .expect("record");
    procedure.steps.insert(
        2,
        ProcedureStep::new("browser.search", PostconditionKind::SearchResultState).taking(vec![
            StepArgument::new(
                "query",
                StepValue::FromPerson {
                    purpose: FieldPurpose::SearchTerms,
                },
            ),
        ]),
    );
    assert_eq!(
        procedure.from_task("task-completed-1"),
        Err(RecordError::UnsupportedRecordedFlow)
    );
}

#[test]
fn format_one_definition_still_decodes_without_a_task_association() {
    // Independent historical wire fixture. No head definition is relabelled.
    fn text(bytes: &mut Vec<u8>, value: &str) {
        bytes.extend_from_slice(
            &u16::try_from(value.len())
                .expect("fixture length")
                .to_be_bytes(),
        );
        bytes.extend_from_slice(value.as_bytes());
    }
    let mut old = b"TFSK\0\x01\0\x01".to_vec();
    text(&mut old, "legacy.read");
    old.extend_from_slice(&1_u32.to_be_bytes());
    for value in ["draft", "recorded_from_task", "https://example.test"] {
        text(&mut old, value);
    }
    old.extend_from_slice(&1_u16.to_be_bytes());
    for value in [
        "role_present",
        "DOCUMENT",
        "browser.dom.read",
        "NO_MUTATION",
    ] {
        text(&mut old, value);
    }
    old.extend_from_slice(&[0, 0, 0]);
    let procedure = decode(&old).expect("legacy format one");
    assert_eq!(procedure.recorded_from_task_id, None);
    assert_eq!(procedure.steps.len(), 1);
    assert_eq!(decode(&encode(&procedure).expect("upgrade")), Ok(procedure));
}
