// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable projection for the closed built-in skill identity.

use core_service_types as wire;
use task_engine::{BuiltinSkillId, BuiltinSkillReference};

use super::ConversionError;

pub(super) const fn persisted_builtin_skill(
    reference: BuiltinSkillReference,
) -> wire::PersistedBuiltinSkillReference {
    wire::PersistedBuiltinSkillReference {
        skill_id: persisted_id(reference.skill_id),
        version: reference.version,
    }
}

pub(super) fn restored_builtin_skill(
    reference: &wire::PersistedBuiltinSkillReference,
) -> Result<BuiltinSkillReference, ConversionError> {
    if reference.version == 0 {
        return Err(ConversionError::InvalidValue);
    }
    Ok(BuiltinSkillReference {
        skill_id: restored_id(reference.skill_id),
        version: reference.version,
    })
}

const fn persisted_id(value: BuiltinSkillId) -> wire::PersistedBuiltinSkillId {
    match value {
        BuiltinSkillId::GeneralWebResearch => wire::PersistedBuiltinSkillId::GeneralWebResearch,
        BuiltinSkillId::DeepResearch => wire::PersistedBuiltinSkillId::DeepResearch,
        BuiltinSkillId::ProductComparison => wire::PersistedBuiltinSkillId::ProductComparison,
        BuiltinSkillId::MultiTabComparison => wire::PersistedBuiltinSkillId::MultiTabComparison,
        BuiltinSkillId::WebsiteSummarizer => wire::PersistedBuiltinSkillId::WebsiteSummarizer,
        BuiltinSkillId::PdfAnalysis => wire::PersistedBuiltinSkillId::PdfAnalysis,
        BuiltinSkillId::DataExtraction => wire::PersistedBuiltinSkillId::DataExtraction,
        BuiltinSkillId::FormAssistant => wire::PersistedBuiltinSkillId::FormAssistant,
        BuiltinSkillId::Shopping => wire::PersistedBuiltinSkillId::Shopping,
        BuiltinSkillId::DownloadOrganizer => wire::PersistedBuiltinSkillId::DownloadOrganizer,
        BuiltinSkillId::TravelResearch => wire::PersistedBuiltinSkillId::TravelResearch,
        BuiltinSkillId::VideoTranscriptAnalyzer => {
            wire::PersistedBuiltinSkillId::VideoTranscriptAnalyzer
        }
        BuiltinSkillId::ImageUnderstanding => wire::PersistedBuiltinSkillId::ImageUnderstanding,
        BuiltinSkillId::LibraryBuilder => wire::PersistedBuiltinSkillId::LibraryBuilder,
        BuiltinSkillId::SpreadsheetBuilder => wire::PersistedBuiltinSkillId::SpreadsheetBuilder,
        BuiltinSkillId::DocumentGenerator => wire::PersistedBuiltinSkillId::DocumentGenerator,
    }
}

const fn restored_id(value: wire::PersistedBuiltinSkillId) -> BuiltinSkillId {
    match value {
        wire::PersistedBuiltinSkillId::GeneralWebResearch => BuiltinSkillId::GeneralWebResearch,
        wire::PersistedBuiltinSkillId::DeepResearch => BuiltinSkillId::DeepResearch,
        wire::PersistedBuiltinSkillId::ProductComparison => BuiltinSkillId::ProductComparison,
        wire::PersistedBuiltinSkillId::MultiTabComparison => BuiltinSkillId::MultiTabComparison,
        wire::PersistedBuiltinSkillId::WebsiteSummarizer => BuiltinSkillId::WebsiteSummarizer,
        wire::PersistedBuiltinSkillId::PdfAnalysis => BuiltinSkillId::PdfAnalysis,
        wire::PersistedBuiltinSkillId::DataExtraction => BuiltinSkillId::DataExtraction,
        wire::PersistedBuiltinSkillId::FormAssistant => BuiltinSkillId::FormAssistant,
        wire::PersistedBuiltinSkillId::Shopping => BuiltinSkillId::Shopping,
        wire::PersistedBuiltinSkillId::DownloadOrganizer => BuiltinSkillId::DownloadOrganizer,
        wire::PersistedBuiltinSkillId::TravelResearch => BuiltinSkillId::TravelResearch,
        wire::PersistedBuiltinSkillId::VideoTranscriptAnalyzer => {
            BuiltinSkillId::VideoTranscriptAnalyzer
        }
        wire::PersistedBuiltinSkillId::ImageUnderstanding => BuiltinSkillId::ImageUnderstanding,
        wire::PersistedBuiltinSkillId::LibraryBuilder => BuiltinSkillId::LibraryBuilder,
        wire::PersistedBuiltinSkillId::SpreadsheetBuilder => BuiltinSkillId::SpreadsheetBuilder,
        wire::PersistedBuiltinSkillId::DocumentGenerator => BuiltinSkillId::DocumentGenerator,
    }
}
