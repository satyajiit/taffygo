// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The fixed built-in ways Taffy can help.
//!
//! A definition narrows a reviewed task template, source shape, output shape,
//! and exact tool set. It grants no authority: every resulting action still
//! crosses the ordinary task and policy gates.

mod binding;
mod catalogue;
mod status;

pub use binding::{saved_procedure_version, BuiltinSkillBindingError};
pub use catalogue::{BuiltinSkillCatalogue, BuiltinSkillCatalogueError};
pub use status::ProductionBuiltinSkills;
pub use task_engine::{BuiltinSkillId, BuiltinSkillReference};

use core_service_types::{AssistantAbility, TaskTemplateId};
use task_engine::Milestone;

pub(crate) const fn builtin_id_from_wire(
    value: core_service_types::BuiltinSkillId,
) -> BuiltinSkillId {
    match value {
        core_service_types::BuiltinSkillId::GeneralWebResearch => {
            BuiltinSkillId::GeneralWebResearch
        }
        core_service_types::BuiltinSkillId::DeepResearch => BuiltinSkillId::DeepResearch,
        core_service_types::BuiltinSkillId::ProductComparison => BuiltinSkillId::ProductComparison,
        core_service_types::BuiltinSkillId::MultiTabComparison => {
            BuiltinSkillId::MultiTabComparison
        }
        core_service_types::BuiltinSkillId::WebsiteSummarizer => BuiltinSkillId::WebsiteSummarizer,
        core_service_types::BuiltinSkillId::PdfAnalysis => BuiltinSkillId::PdfAnalysis,
        core_service_types::BuiltinSkillId::DataExtraction => BuiltinSkillId::DataExtraction,
        core_service_types::BuiltinSkillId::FormAssistant => BuiltinSkillId::FormAssistant,
        core_service_types::BuiltinSkillId::Shopping => BuiltinSkillId::Shopping,
        core_service_types::BuiltinSkillId::DownloadOrganizer => BuiltinSkillId::DownloadOrganizer,
        core_service_types::BuiltinSkillId::TravelResearch => BuiltinSkillId::TravelResearch,
        core_service_types::BuiltinSkillId::VideoTranscriptAnalyzer => {
            BuiltinSkillId::VideoTranscriptAnalyzer
        }
        core_service_types::BuiltinSkillId::ImageUnderstanding => {
            BuiltinSkillId::ImageUnderstanding
        }
        core_service_types::BuiltinSkillId::LibraryBuilder => BuiltinSkillId::LibraryBuilder,
        core_service_types::BuiltinSkillId::SpreadsheetBuilder => {
            BuiltinSkillId::SpreadsheetBuilder
        }
        core_service_types::BuiltinSkillId::DocumentGenerator => BuiltinSkillId::DocumentGenerator,
    }
}

/// Closed source policy owned by a built-in definition.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BuiltinSkillSourcePolicy {
    SelectedPage,
    SelectedPages {
        minimum: u32,
        maximum: u32,
    },
    DiscoverWeb {
        maximum_named_sources: u32,
        maximum_new_sources: u32,
    },
    CurrentPageAndDiscoveredDestinations,
    DownloadHistoryAndDiscoveredPage,
}

/// Closed output policy owned by a built-in definition.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BuiltinSkillOutputPolicy {
    CitedAnswer,
    ComparisonTable,
    CitedSummary,
    TableArtifact,
    PreparedForm,
    DownloadPlan,
    CitedBrief,
    CitedMediaAnswer,
    LibraryMutation,
    SpreadsheetArtifact,
    DocumentArtifact,
}

/// Planned tool identities allowed to be absent from the shipping registry.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlannedTool {
    DownloadClassify,
    DownloadRename,
    DownloadMove,
    DownloadUndo,
}

impl PlannedTool {
    /// The entire frozen planned set, in review order.
    pub const ALL: &'static [Self] = &[
        Self::DownloadClassify,
        Self::DownloadRename,
        Self::DownloadMove,
        Self::DownloadUndo,
    ];

    /// Exact internal name reserved for this planned row.
    pub const fn name(self) -> &'static str {
        match self {
            Self::DownloadClassify => "browser.download.classify",
            Self::DownloadRename => "browser.download.rename",
            Self::DownloadMove => "browser.download.move",
            Self::DownloadUndo => "browser.download.undo",
        }
    }

    const fn index(self) -> usize {
        match self {
            Self::DownloadClassify => 0,
            Self::DownloadRename => 1,
            Self::DownloadMove => 2,
            Self::DownloadUndo => 3,
        }
    }
}

/// One exact tool requirement. Unresolved string names have no variant.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RequiredTool {
    Registered(&'static str),
    Planned(PlannedTool),
}

/// One exact product-part requirement.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RequiredAsset {
    pub asset_id: &'static str,
    pub asset_revision: &'static str,
}

/// Complete trusted definition of one built-in skill.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BuiltinSkillDefinition {
    pub id: BuiltinSkillId,
    pub version: u32,
    pub required_ability: AssistantAbility,
    pub template: TaskTemplateId,
    pub milestone: Milestone,
    pub source_policy: BuiltinSkillSourcePolicy,
    pub output_policy: BuiltinSkillOutputPolicy,
    pub required_tools: &'static [RequiredTool],
    pub required_assets: &'static [RequiredAsset],
}

#[cfg(test)]
mod tests;
