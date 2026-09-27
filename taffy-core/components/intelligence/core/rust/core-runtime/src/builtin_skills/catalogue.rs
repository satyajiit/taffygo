// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one compiled catalogue and its construction-time proof.

use core_service_types::{AssistantAbility as Ability, TaskTemplateId as Template};
use task_engine::Milestone;

use super::{
    BuiltinSkillDefinition as Definition, BuiltinSkillId as Id, BuiltinSkillOutputPolicy as Output,
    BuiltinSkillSourcePolicy as Source, RequiredAsset, RequiredTool,
};

mod tools;
mod validation;

use tools::{
    DATA, DEEP, DOCUMENT, DOWNLOAD, FORM, GENERAL, IMAGE, LIBRARY, MULTI, PDF, PRODUCT, SHEET,
    SHOPPING, SUMMARY, TRAVEL, VIDEO,
};
use validation::{registered_entry, validate_definitions};

const NO_ASSETS: &[RequiredAsset] = &[];
const SELECTED_ONE_TO_SIXTEEN: Source = Source::SelectedPages {
    minimum: 1,
    maximum: 16,
};
const SELECTED_TWO_TO_SIXTEEN: Source = Source::SelectedPages {
    minimum: 2,
    maximum: 16,
};
const DISCOVER_WEB: Source = Source::DiscoverWeb {
    maximum_named_sources: 1,
    maximum_new_sources: task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP,
};

const DEFINITIONS: &[Definition] = &[
    definition(
        Id::GeneralWebResearch,
        Ability::PagesLookup,
        Template::SummarizeEvidence,
        Milestone::M3,
        SELECTED_ONE_TO_SIXTEEN,
        Output::CitedAnswer,
        GENERAL,
    ),
    definition(
        Id::DeepResearch,
        Ability::Depth,
        Template::WebErrand,
        Milestone::M3,
        DISCOVER_WEB,
        Output::CitedAnswer,
        DEEP,
    ),
    definition(
        Id::ProductComparison,
        Ability::Products,
        Template::CompareProducts,
        Milestone::M3,
        SELECTED_TWO_TO_SIXTEEN,
        Output::ComparisonTable,
        PRODUCT,
    ),
    definition(
        Id::MultiTabComparison,
        Ability::PagesCompare,
        Template::CompareProducts,
        Milestone::M3,
        SELECTED_TWO_TO_SIXTEEN,
        Output::ComparisonTable,
        MULTI,
    ),
    definition(
        Id::WebsiteSummarizer,
        Ability::PagesSummarize,
        Template::SummarizeEvidence,
        Milestone::M3,
        Source::SelectedPage,
        Output::CitedSummary,
        SUMMARY,
    ),
    definition(
        Id::PdfAnalysis,
        Ability::Pdf,
        Template::SummarizeEvidence,
        Milestone::M4,
        Source::SelectedPage,
        Output::CitedAnswer,
        PDF,
    ),
    definition(
        Id::DataExtraction,
        Ability::PagesTable,
        Template::BuildSourceTable,
        Milestone::M4,
        Source::SelectedPage,
        Output::TableArtifact,
        DATA,
    ),
    definition(
        Id::FormAssistant,
        Ability::Form,
        Template::WebErrand,
        Milestone::M5,
        Source::CurrentPageAndDiscoveredDestinations,
        Output::PreparedForm,
        FORM,
    ),
    definition(
        Id::Shopping,
        Ability::Offers,
        Template::WebErrand,
        Milestone::M5,
        DISCOVER_WEB,
        Output::ComparisonTable,
        SHOPPING,
    ),
    definition(
        Id::DownloadOrganizer,
        Ability::Downloads,
        Template::WebErrand,
        Milestone::M5,
        Source::DownloadHistoryAndDiscoveredPage,
        Output::DownloadPlan,
        DOWNLOAD,
    ),
    definition(
        Id::TravelResearch,
        Ability::Trip,
        Template::WebErrand,
        Milestone::M6,
        DISCOVER_WEB,
        Output::CitedBrief,
        TRAVEL,
    ),
    definition(
        Id::VideoTranscriptAnalyzer,
        Ability::Video,
        Template::SummarizeEvidence,
        Milestone::M6,
        Source::SelectedPage,
        Output::CitedMediaAnswer,
        VIDEO,
    ),
    definition(
        Id::ImageUnderstanding,
        Ability::Pictures,
        Template::SummarizeEvidence,
        Milestone::M6,
        Source::SelectedPage,
        Output::CitedMediaAnswer,
        IMAGE,
    ),
    definition(
        Id::LibraryBuilder,
        Ability::Keep,
        Template::SummarizeEvidence,
        Milestone::M6,
        Source::SelectedPage,
        Output::LibraryMutation,
        LIBRARY,
    ),
    definition(
        Id::SpreadsheetBuilder,
        Ability::Sheet,
        Template::BuildSourceTable,
        Milestone::M7,
        Source::SelectedPage,
        Output::SpreadsheetArtifact,
        SHEET,
    ),
    definition(
        Id::DocumentGenerator,
        Ability::Document,
        Template::SummarizeEvidence,
        Milestone::M7,
        SELECTED_ONE_TO_SIXTEEN,
        Output::DocumentArtifact,
        DOCUMENT,
    ),
];

const fn definition(
    id: Id,
    required_ability: Ability,
    template: Template,
    milestone: Milestone,
    source_policy: Source,
    output_policy: Output,
    required_tools: &'static [RequiredTool],
) -> Definition {
    Definition {
        id,
        version: 1,
        required_ability,
        template,
        milestone,
        source_policy,
        output_policy,
        required_tools,
        required_assets: NO_ASSETS,
    }
}

/// Why the compiled catalogue was not internally coherent.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BuiltinSkillCatalogueError {
    DefinitionSet,
    DefinitionVersion,
    DefinitionMilestone,
    InvalidSourcePolicy,
    MissingRequiredTool,
    DuplicateRequiredTool,
    RegisteredToolUnavailable(&'static str),
    PlannedToolRegistered,
    PlannedToolSet,
    DuplicateRequiredAsset,
}

/// Validated read-only view of the compiled catalogue.
#[derive(Clone, Copy, Debug)]
pub struct BuiltinSkillCatalogue {
    definitions: &'static [Definition],
}

impl BuiltinSkillCatalogue {
    /// Validates the shipping definition table against the shipping registry.
    pub fn production() -> Result<Self, BuiltinSkillCatalogueError> {
        validate_definitions(DEFINITIONS)?;
        Ok(Self {
            definitions: DEFINITIONS,
        })
    }

    /// Every definition in fixed public order.
    pub const fn definitions(&self) -> &'static [Definition] {
        self.definitions
    }

    pub(super) fn registered_tool_is_available(name: &str, milestone: Milestone) -> bool {
        registered_entry(name, milestone).is_some()
    }
}
