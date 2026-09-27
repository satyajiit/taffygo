// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Core API status projection of the validated catalogue.

use crate::assistant_configuration::AssistantConfiguration;
use crate::ports::{AssetInstallationView, StatusContributionFacts, StatusContributorPort};

use super::{BuiltinSkillCatalogue, BuiltinSkillId, RequiredTool};
use crate::product_capabilities::ACTIVE;

impl BuiltinSkillCatalogue {
    pub(crate) fn status_views(
        &self,
        configuration: &AssistantConfiguration,
        installations: &[AssetInstallationView],
    ) -> Vec<core_api_types::BuiltinSkillView> {
        self.definitions()
            .iter()
            .map(|definition| {
                let available_tools = ACTIVE.task_milestone.map_or(0, |milestone| {
                    definition
                        .required_tools
                        .iter()
                        .filter(|tool| match tool {
                            RequiredTool::Registered(name) => {
                                Self::registered_tool_is_available(name, milestone)
                            }
                            RequiredTool::Planned(_) => false,
                        })
                        .count()
                });
                let installed_parts = definition
                    .required_assets
                    .iter()
                    .filter(|required| {
                        installations.iter().any(|installed| {
                            installed.asset_id == required.asset_id
                                && installed.asset_revision == required.asset_revision
                                && installed.presence
                                    == core_service_types::AssetPresence::Installed
                        })
                    })
                    .count();
                let milestone_reached = ACTIVE
                    .task_milestone
                    .is_some_and(|milestone| milestone >= definition.milestone);
                let availability =
                    if !milestone_reached || available_tools != definition.required_tools.len() {
                        core_api_types::BuiltinSkillAvailabilityView::RequiredToolUnavailable
                    } else if installed_parts != definition.required_assets.len() {
                        core_api_types::BuiltinSkillAvailabilityView::RequiredPartMissing
                    } else {
                        core_api_types::BuiltinSkillAvailabilityView::Available
                    };
                core_api_types::BuiltinSkillView {
                    reference: core_api_types::BuiltinSkillReferenceView {
                        skill_id: api_id(definition.id),
                        version: definition.version,
                    },
                    required_ability: crate::assistant_configuration::api_ability(
                        definition.required_ability,
                    ),
                    enabled: configuration.ability_is_enabled(definition.required_ability),
                    availability,
                    required_tool_count: bounded_count(definition.required_tools.len()),
                    available_tool_count: bounded_count(available_tools),
                    required_part_count: bounded_count(definition.required_assets.len()),
                    installed_part_count: bounded_count(installed_parts),
                }
            })
            .collect()
    }
}

/// Writes only the validated fixed built-in rows into status.
#[derive(Clone, Copy, Debug, Default)]
pub struct ProductionBuiltinSkills;

impl StatusContributorPort for ProductionBuiltinSkills {
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    ) {
        status.builtin_skills = facts.builtin_skills.to_vec();
    }
}

const fn api_id(id: BuiltinSkillId) -> core_api_types::BuiltinSkillIdView {
    match id {
        BuiltinSkillId::GeneralWebResearch => {
            core_api_types::BuiltinSkillIdView::GeneralWebResearch
        }
        BuiltinSkillId::DeepResearch => core_api_types::BuiltinSkillIdView::DeepResearch,
        BuiltinSkillId::ProductComparison => core_api_types::BuiltinSkillIdView::ProductComparison,
        BuiltinSkillId::MultiTabComparison => {
            core_api_types::BuiltinSkillIdView::MultiTabComparison
        }
        BuiltinSkillId::WebsiteSummarizer => core_api_types::BuiltinSkillIdView::WebsiteSummarizer,
        BuiltinSkillId::PdfAnalysis => core_api_types::BuiltinSkillIdView::PdfAnalysis,
        BuiltinSkillId::DataExtraction => core_api_types::BuiltinSkillIdView::DataExtraction,
        BuiltinSkillId::FormAssistant => core_api_types::BuiltinSkillIdView::FormAssistant,
        BuiltinSkillId::Shopping => core_api_types::BuiltinSkillIdView::Shopping,
        BuiltinSkillId::DownloadOrganizer => core_api_types::BuiltinSkillIdView::DownloadOrganizer,
        BuiltinSkillId::TravelResearch => core_api_types::BuiltinSkillIdView::TravelResearch,
        BuiltinSkillId::VideoTranscriptAnalyzer => {
            core_api_types::BuiltinSkillIdView::VideoTranscriptAnalyzer
        }
        BuiltinSkillId::ImageUnderstanding => {
            core_api_types::BuiltinSkillIdView::ImageUnderstanding
        }
        BuiltinSkillId::LibraryBuilder => core_api_types::BuiltinSkillIdView::LibraryBuilder,
        BuiltinSkillId::SpreadsheetBuilder => {
            core_api_types::BuiltinSkillIdView::SpreadsheetBuilder
        }
        BuiltinSkillId::DocumentGenerator => core_api_types::BuiltinSkillIdView::DocumentGenerator,
    }
}

fn bounded_count(value: usize) -> u32 {
    u32::try_from(value).unwrap_or(u32::MAX)
}
