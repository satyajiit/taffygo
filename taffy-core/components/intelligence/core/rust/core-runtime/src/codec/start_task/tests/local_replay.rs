// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact saved-flow admission crosses the real profile catalogue and decoder.

use std::rc::Rc;

use bip_types::{action::PostconditionKind, snapshot::SemanticRole};
use core_service_types as wire;
use procedure_engine::{
    MatchClause, MatchCondition, Procedure, ProcedureId, ProcedureProvenance, ProcedureScope,
    ProcedureStep, StepArgument,
};
use task_engine::tool::ArgumentValue;

use crate::account::crypto::ReferenceSha256;
use crate::contract::ServiceGeneration;
use crate::{create_profile_service_runtime, decode_start_task, ProfileRuntimeConfiguration};

#[test]
fn only_the_accepted_exact_runnable_recording_starts_without_provider_setup() {
    let mut runtime = create_profile_service_runtime(
        ProfileRuntimeConfiguration {
            generation: ServiceGeneration::INITIAL,
            generation_capability_entropy: core::array::from_fn(|index| {
                u8::try_from(index).unwrap()
            }),
            initial_utc_millis: 1_000,
            private_profile: false,
            browser_profile_id: "profile-a".to_owned(),
            browser_session_id: "browser-session-1".to_owned(),
            available_account_methods: Vec::new(),
            skills: Vec::new(),
            recall: Vec::new(),
            assistant_configuration: None,
        },
        Rc::new(ReferenceSha256),
    )
    .unwrap();
    let recorded = runtime
        .prepare_recorded_procedure(recording(), 1_000)
        .unwrap();
    runtime.install_skill_mutation(recorded).unwrap();
    let start = local_start();
    assert!(
        runtime.prepare_skill_start(&mut start.clone()).is_err(),
        "draft was not accepted"
    );
    let acceptance = wire::MutateSkillCommand {
        kind: wire::SkillMutationKind::SetEnabled,
        skill_id: "recorded-flow".to_owned(),
        origin: String::new(),
        expected_version: 1,
        enabled: true,
        clauses: Vec::new(),
        steps: Vec::new(),
        admitted: 0,
        recorded_at_epoch_ms: 1_100,
    };
    let accepted = runtime.prepare_skill_mutation(&acceptance).unwrap();
    runtime.install_skill_mutation(accepted).unwrap();
    let mut runnable = start.clone();
    runtime.prepare_skill_start(&mut runnable).unwrap();
    assert_eq!(
        runnable.tool_allowlist,
        vec!["browser.navigate", "browser.dom.read", "user.handover"]
    );
    assert!(decode_start_task(&runnable, &super::operation()).is_ok());
    let mut stale = start.clone();
    stale.skill_version_id = Some("recorded-flow@2".to_owned());
    assert!(runtime.prepare_skill_start(&mut stale).is_err());
    let mut wrong_site = start.clone();
    wrong_site.consent_preview.sources[0].normalized_origin = "https://other.test".to_owned();
    assert!(runtime.prepare_skill_start(&mut wrong_site).is_err());
    let mut disabled = acceptance;
    disabled.enabled = false;
    let disabled = runtime.prepare_skill_mutation(&disabled).unwrap();
    runtime.install_skill_mutation(disabled).unwrap();
    assert!(runtime.prepare_skill_start(&mut start.clone()).is_err());
}

fn local_start() -> wire::StartTaskCommand {
    let mut start = super::start();
    start.template_id = wire::TaskTemplateId::WebErrand;
    start.kind = wire::TaskKind::Errand;
    start.skill_version_id = Some("recorded-flow@1".to_owned());
    start.tool_allowlist = vec![
        "browser.dom.read".to_owned(),
        "browser.navigate".to_owned(),
        "browser.search".to_owned(),
    ];
    start
}

fn recording() -> Procedure {
    Procedure::draft(
        ProcedureId::new("recorded-flow").unwrap(),
        ProcedureScope::for_origin("https://example.test").unwrap(),
        MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::Document)]),
        vec![
            ProcedureStep::new("browser.navigate", PostconditionKind::CommittedNavigation).taking(
                vec![StepArgument::literal(
                    "address",
                    ArgumentValue::Address("https://example.test/start".to_owned()),
                )],
            ),
            ProcedureStep::new("browser.dom.read", PostconditionKind::NoMutation),
        ],
        ProcedureProvenance::RecordedFromTask,
    )
    .from_task("completed-source-task")
    .unwrap()
}
