// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_service_types as wire;
use task_engine::{BuiltinSkillId, BuiltinSkillReference, TaskTemplateId};

use super::*;
use crate::builtin_skills::BuiltinSkillDefinition;

fn source(index: usize) -> wire::TaskConsentSource {
    wire::TaskConsentSource {
        source_id: format!("source-{index}"),
        tab_id: format!("tab-{index}"),
        normalized_origin: format!("https://source-{index}.example"),
        canonical_locator: None,
    }
}

fn wire_id(value: BuiltinSkillId) -> wire::BuiltinSkillId {
    wire::BuiltinSkillId::from_wire(value as u32).unwrap_or_else(|| unreachable!())
}

fn sources_for(definition: &BuiltinSkillDefinition) -> (Vec<wire::TaskConsentSource>, bool, u32) {
    match definition.source_policy {
        BuiltinSkillSourcePolicy::SelectedPage => (vec![source(0)], false, 0),
        BuiltinSkillSourcePolicy::SelectedPages { minimum, .. } => (
            (0..minimum)
                .map(|index| source(usize::try_from(index).unwrap_or_default()))
                .collect(),
            false,
            0,
        ),
        BuiltinSkillSourcePolicy::DiscoverWeb { .. }
        | BuiltinSkillSourcePolicy::DownloadHistoryAndDiscoveredPage => {
            (Vec::new(), true, task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP)
        }
        BuiltinSkillSourcePolicy::CurrentPageAndDiscoveredDestinations => (
            vec![source(0)],
            true,
            task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP,
        ),
    }
}

fn start(definition: &BuiltinSkillDefinition) -> wire::StartTaskCommand {
    let (sources, discovery, cap) = sources_for(definition);
    wire::StartTaskCommand {
        task_id: "task-built-in".to_owned(),
        workspace_id: None,
        browser_profile_id: "profile-built-in".to_owned(),
        kind: wire::TaskKind::Research,
        goal: "Use the selected built-in".to_owned(),
        control_mode: wire::TaskControlMode::Assistant,
        provider_route_id: Some("managed_service".to_owned()),
        assistant_config_version: 1,
        policy_version: 1,
        skill_version_id: None,
        builtin_skill: Some(wire::BuiltinSkillReference {
            skill_id: wire_id(definition.id),
            version: definition.version,
        }),
        tool_allowlist: Vec::new(),
        milestone: wire::TaskMilestone::M7,
        budgets: Vec::new(),
        has_task_deadline: false,
        task_deadline_monotonic_ms: 0,
        task_deadline_utc_ms: 0,
        predecessor_task_id: None,
        trace_id: "trace-built-in".to_owned(),
        task_id_seed: core::array::from_fn(|index| u8::try_from(index).unwrap_or_default()),
        template_id: definition.template,
        consent_preview: wire::TaskConsentPreview {
            sources,
            source_discovery_enabled: discovery,
            new_source_cap: cap,
            provider_route: wire::TaskProviderRoute::ManagedService,
        },
        initial_consent_receipt_id: "consent-built-in".to_owned(),
        browser_session_id: "browser-session-built-in".to_owned(),
        library_refresh: None,
    }
}

fn definition(catalogue: &BuiltinSkillCatalogue, id: BuiltinSkillId) -> BuiltinSkillDefinition {
    *catalogue
        .definitions()
        .iter()
        .find(|definition| definition.id == id)
        .unwrap_or_else(|| unreachable!())
}

fn disabled(ability: wire::AssistantAbility) -> AssistantConfiguration {
    AssistantConfiguration::restore(Some(wire::AssistantConfiguration {
        revision: 1,
        disabled_abilities: vec![ability],
        preset: wire::PersonalityPreset::CarefulResearcher,
        pace: 0,
        length: 1,
        check_in: 0,
    }))
    .unwrap_or_else(|_| unreachable!())
}

#[test]
fn every_complete_definition_binds_at_the_current_product_milestone() {
    let catalogue = BuiltinSkillCatalogue::production().unwrap_or_else(|_| unreachable!());
    for definition in catalogue.definitions() {
        let mut command = start(definition);
        let result = catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut command);
        if definition.id == BuiltinSkillId::DownloadOrganizer {
            assert_eq!(
                result,
                Err(BuiltinSkillBindingError::RequiredToolUnavailable)
            );
        } else {
            assert_eq!(result, Ok(true), "{:?}", definition.id);
            assert!(!command.tool_allowlist.is_empty());
        }
    }
}

#[test]
fn start_refuses_conflict_version_template_source_ability_and_milestone_drift() {
    let catalogue = BuiltinSkillCatalogue::production().unwrap_or_else(|_| unreachable!());
    let definition = definition(&catalogue, BuiltinSkillId::WebsiteSummarizer);

    let mut conflict = start(&definition);
    conflict.skill_version_id = Some("saved@1".to_owned());
    assert_eq!(
        catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut conflict),
        Err(BuiltinSkillBindingError::ConflictingSavedProcedure)
    );

    let mut wrong_version = start(&definition);
    wrong_version.builtin_skill.as_mut().unwrap().version = 2;
    assert_eq!(
        catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut wrong_version),
        Err(BuiltinSkillBindingError::WrongVersion)
    );

    let mut wrong_template = start(&definition);
    wrong_template.template_id = wire::TaskTemplateId::CompareProducts;
    assert_eq!(
        catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut wrong_template),
        Err(BuiltinSkillBindingError::WrongTemplate)
    );

    let mut wrong_source = start(&definition);
    wrong_source.consent_preview.sources.clear();
    assert_eq!(
        catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut wrong_source),
        Err(BuiltinSkillBindingError::SourcePolicyMismatch)
    );

    let mut disabled_start = start(&definition);
    assert_eq!(
        catalogue.prepare_start(
            &disabled(definition.required_ability),
            &[],
            &mut disabled_start,
        ),
        Err(BuiltinSkillBindingError::AbilityDisabled)
    );

    let mut too_early = start(&definition);
    too_early.milestone = wire::TaskMilestone::M2;
    assert_eq!(
        catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut too_early),
        Err(BuiltinSkillBindingError::MilestoneNotReached)
    );
}

#[test]
fn tool_binding_requires_every_definition_row_and_drops_extra_names() {
    let catalogue = BuiltinSkillCatalogue::production().unwrap_or_else(|_| unreachable!());
    let definition = definition(&catalogue, BuiltinSkillId::GeneralWebResearch);
    let mut missing = start(&definition);
    missing.tool_allowlist = vec!["browser.dom.read".to_owned()];
    assert_eq!(
        catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut missing),
        Err(BuiltinSkillBindingError::RequiredToolUnavailable)
    );

    let mut extra = start(&definition);
    extra.tool_allowlist = definition
        .required_tools
        .iter()
        .filter_map(|required| match required {
            RequiredTool::Registered(name) => {
                Some(task_engine::tool::allowlist_name(name).to_owned())
            }
            RequiredTool::Planned(_) => None,
        })
        .chain(["invented.tool".to_owned()])
        .collect();
    assert_eq!(
        catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut extra),
        Ok(true)
    );
    assert!(!extra
        .tool_allowlist
        .iter()
        .any(|name| name == "invented.tool"));
    assert_eq!(
        extra
            .tool_allowlist
            .iter()
            .filter(|name| name.as_str() == "browser.tabs")
            .count(),
        1
    );
}

#[test]
fn restore_uses_durable_binding_without_current_configuration() {
    let catalogue = BuiltinSkillCatalogue::production().unwrap_or_else(|_| unreachable!());
    let definition = definition(&catalogue, BuiltinSkillId::WebsiteSummarizer);
    let mut command = start(&definition);
    assert_eq!(
        catalogue.prepare_start(&AssistantConfiguration::default(), &[], &mut command),
        Ok(true)
    );
    let facts = BuiltinSkillBindingFacts {
        reference: BuiltinSkillReference {
            skill_id: definition.id,
            version: definition.version,
        },
        template_id: TaskTemplateId::SummarizeEvidence,
        tool_allowlist: command.tool_allowlist,
        milestone: task_engine::Milestone::M7,
        initial_source_count: 1,
        source_discovery_enabled: false,
        remaining_new_source_cap: 0,
    };
    assert_eq!(catalogue.validate_restore(&facts), Ok(()));

    let mut drifted = facts;
    drifted.tool_allowlist.push("browser.navigate".to_owned());
    assert_eq!(
        catalogue.validate_restore(&drifted),
        Err(BuiltinSkillBindingError::ToolBindingMismatch)
    );
}

#[test]
fn asset_requirement_needs_the_exact_installed_revision() {
    let required = RequiredAsset {
        asset_id: "part-a",
        asset_revision: "revision-2",
    };
    let mut installed = AssetInstallationView {
        asset_id: "part-a".to_owned(),
        asset_revision: "revision-1".to_owned(),
        kind: wire::AssetKind::FilterList,
        presence: wire::AssetPresence::Installed,
        written_bytes: 1,
        total_bytes: 1,
        attempts: 0,
        retry_after_monotonic_ms: 0,
        refusal: None,
        refusal_retryable: false,
    };
    assert!(!asset_is_installed(&required, &[installed.clone()]));
    installed.asset_revision = "revision-2".to_owned();
    assert!(asset_is_installed(&required, &[installed]));
}

#[test]
fn builtins_use_the_ordinary_walk_and_never_name_a_saved_run() {
    let builtin = Some(BuiltinSkillReference {
        skill_id: BuiltinSkillId::GeneralWebResearch,
        version: 1,
    });
    assert_eq!(saved_procedure_version(builtin, Some("saved@9")), None);
    assert_eq!(
        saved_procedure_version(None, Some("saved@9")),
        Some("saved@9")
    );
    assert_eq!(saved_procedure_version(None, None), None);
}
