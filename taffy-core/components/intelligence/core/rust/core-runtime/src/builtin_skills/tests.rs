// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_service_types::{AssistantAbility as Ability, TaskTemplateId as Template};

use super::{
    BuiltinSkillCatalogue, BuiltinSkillId as Id, BuiltinSkillOutputPolicy as Output, PlannedTool,
    RequiredTool,
};
use crate::assistant_configuration::AssistantConfiguration;

#[test]
fn production_catalogue_is_complete_and_every_nonplanned_tool_is_served() {
    let catalogue = BuiltinSkillCatalogue::production()
        .unwrap_or_else(|error| panic!("production catalogue must validate: {error:?}"));
    assert_eq!(catalogue.definitions().len(), Id::ALL.len());
    for (definition, id) in catalogue.definitions().iter().zip(Id::ALL) {
        assert_eq!(definition.id, *id);
        assert_eq!(definition.version, 1);
        assert!(!definition.required_tools.is_empty());
        assert!(definition.required_assets.is_empty());
        for requirement in definition.required_tools {
            if let RequiredTool::Registered(name) = requirement {
                assert!(
                    BuiltinSkillCatalogue::registered_tool_is_available(
                        name,
                        task_engine::Milestone::M7,
                    ),
                    "{name}"
                );
            }
        }
    }
}

#[test]
fn definition_milestones_match_the_authoritative_feature_catalogue() {
    let catalogue = BuiltinSkillCatalogue::production()
        .unwrap_or_else(|error| panic!("production catalogue must validate: {error:?}"));
    let expected = [
        task_engine::Milestone::M3,
        task_engine::Milestone::M3,
        task_engine::Milestone::M3,
        task_engine::Milestone::M3,
        task_engine::Milestone::M3,
        task_engine::Milestone::M4,
        task_engine::Milestone::M4,
        task_engine::Milestone::M5,
        task_engine::Milestone::M5,
        task_engine::Milestone::M5,
        task_engine::Milestone::M6,
        task_engine::Milestone::M6,
        task_engine::Milestone::M6,
        task_engine::Milestone::M6,
        task_engine::Milestone::M7,
        task_engine::Milestone::M7,
    ];
    assert_eq!(
        catalogue
            .definitions()
            .iter()
            .map(|definition| definition.milestone)
            .collect::<Vec<_>>(),
        expected
    );
}

#[test]
fn definitions_keep_the_fixed_ability_template_and_output_mapping() {
    let catalogue = BuiltinSkillCatalogue::production()
        .unwrap_or_else(|error| panic!("production catalogue must validate: {error:?}"));
    let expected = [
        (
            Ability::PagesLookup,
            Template::SummarizeEvidence,
            Output::CitedAnswer,
        ),
        (Ability::Depth, Template::WebErrand, Output::CitedAnswer),
        (
            Ability::Products,
            Template::CompareProducts,
            Output::ComparisonTable,
        ),
        (
            Ability::PagesCompare,
            Template::CompareProducts,
            Output::ComparisonTable,
        ),
        (
            Ability::PagesSummarize,
            Template::SummarizeEvidence,
            Output::CitedSummary,
        ),
        (
            Ability::Pdf,
            Template::SummarizeEvidence,
            Output::CitedAnswer,
        ),
        (
            Ability::PagesTable,
            Template::BuildSourceTable,
            Output::TableArtifact,
        ),
        (Ability::Form, Template::WebErrand, Output::PreparedForm),
        (
            Ability::Offers,
            Template::WebErrand,
            Output::ComparisonTable,
        ),
        (
            Ability::Downloads,
            Template::WebErrand,
            Output::DownloadPlan,
        ),
        (Ability::Trip, Template::WebErrand, Output::CitedBrief),
        (
            Ability::Video,
            Template::SummarizeEvidence,
            Output::CitedMediaAnswer,
        ),
        (
            Ability::Pictures,
            Template::SummarizeEvidence,
            Output::CitedMediaAnswer,
        ),
        (
            Ability::Keep,
            Template::SummarizeEvidence,
            Output::LibraryMutation,
        ),
        (
            Ability::Sheet,
            Template::BuildSourceTable,
            Output::SpreadsheetArtifact,
        ),
        (
            Ability::Document,
            Template::SummarizeEvidence,
            Output::DocumentArtifact,
        ),
    ];
    for (definition, expected) in catalogue.definitions().iter().zip(expected) {
        assert_eq!(
            (
                definition.required_ability,
                definition.template,
                definition.output_policy
            ),
            expected
        );
    }
}

#[test]
fn status_keeps_all_rows_and_only_download_organizer_lacks_tools() {
    let catalogue = BuiltinSkillCatalogue::production()
        .unwrap_or_else(|error| panic!("production catalogue must validate: {error:?}"));
    let views = catalogue.status_views(&AssistantConfiguration::default(), &[]);
    assert_eq!(views.len(), 16);
    for (index, view) in views.iter().enumerate() {
        assert_eq!(view.reference.skill_id as usize, index);
        assert_eq!(view.reference.version, 1);
        assert!(view.enabled);
        if index == 9 {
            assert_eq!(
                view.availability,
                core_api_types::BuiltinSkillAvailabilityView::RequiredToolUnavailable
            );
            assert_eq!(
                (view.required_tool_count, view.available_tool_count),
                (10, 6)
            );
        } else {
            assert_eq!(
                view.availability,
                core_api_types::BuiltinSkillAvailabilityView::Available
            );
            assert_eq!(view.required_tool_count, view.available_tool_count);
        }
        assert_eq!(
            (view.required_part_count, view.installed_part_count),
            (0, 0)
        );
    }
}

#[test]
fn planned_download_names_are_exact_and_unregistered() {
    let expected = [
        "browser.download.classify",
        "browser.download.rename",
        "browser.download.move",
        "browser.download.undo",
    ];
    for (planned, name) in PlannedTool::ALL.iter().zip(expected) {
        assert_eq!(planned.name(), name);
        assert!(task_engine::REGISTRY.iter().all(|entry| entry.name != name));
        assert!(matches!(
            task_engine::resolve(name, task_engine::Milestone::M8),
            task_engine::ToolLookup::Unknown
        ));
    }
}
