// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The complete status fixture behind the cross-language golden, and the empty one.

mod providers;
mod records;
mod skills;

use core_api_types::{
    AssistantAbilityView, AssistantConfigurationView, BuiltinSkillAvailabilityView,
    BuiltinSkillIdView, BuiltinSkillReferenceView, BuiltinSkillView, CoreAvailability, CoreStatus,
    CoreStatusProjectionMode, LibraryAvailability, LibraryViewState, MemoryAvailability,
    MemoryViewState, PersonalityPresetView, SavedDataAvailability, SavedDetailsView,
    SavedSignInsView,
};
use providers::{models_fixture, probes_fixture, roster_fixture};
use records::{
    asset_fixture, auth_fixture, library_export_fixture, library_fixture, memory_fixture,
    saved_details_fixture, saved_sign_ins_fixture, task_fixture, workspace_fixture,
};

pub(super) fn status_fixture() -> CoreStatus {
    CoreStatus {
        availability: CoreAvailability::Ready,
        generation: 11,
        active_tasks: vec![task_fixture()],
        auth_state: Some(auth_fixture()),
        workspaces: vec![workspace_fixture()],
        workspace_export: None,
        asset_delivery: Some(asset_fixture()),
        provider_roster: roster_fixture(),
        provider_probes: probes_fixture(),
        provider_models: models_fixture(),
        assistant_configuration: AssistantConfigurationView {
            revision: 7,
            disabled_abilities: vec![AssistantAbilityView::Downloads, AssistantAbilityView::Video],
            preset: PersonalityPresetView::TripPlanner,
            pace: 1,
            length: 2,
            check_in: 1,
        },
        library: library_fixture(),
        library_export: Some(library_export_fixture()),
        memory: memory_fixture(),
        saved_sign_ins: saved_sign_ins_fixture(),
        saved_details: saved_details_fixture(),
        site_skills: skills::skills_fixture(),
        builtin_skills: builtin_skills_fixture(&[
            AssistantAbilityView::Downloads,
            AssistantAbilityView::Video,
        ]),
        projection_mode: CoreStatusProjectionMode::Complete,
        projection_omissions: Vec::new(),
    }
}

fn builtin_skills_fixture(disabled: &[AssistantAbilityView]) -> Vec<BuiltinSkillView> {
    let abilities = [
        AssistantAbilityView::PagesLookup,
        AssistantAbilityView::Depth,
        AssistantAbilityView::Products,
        AssistantAbilityView::PagesCompare,
        AssistantAbilityView::PagesSummarize,
        AssistantAbilityView::Pdf,
        AssistantAbilityView::PagesTable,
        AssistantAbilityView::Form,
        AssistantAbilityView::Offers,
        AssistantAbilityView::Downloads,
        AssistantAbilityView::Trip,
        AssistantAbilityView::Video,
        AssistantAbilityView::Pictures,
        AssistantAbilityView::Keep,
        AssistantAbilityView::Sheet,
        AssistantAbilityView::Document,
    ];
    let ids = [
        BuiltinSkillIdView::GeneralWebResearch,
        BuiltinSkillIdView::DeepResearch,
        BuiltinSkillIdView::ProductComparison,
        BuiltinSkillIdView::MultiTabComparison,
        BuiltinSkillIdView::WebsiteSummarizer,
        BuiltinSkillIdView::PdfAnalysis,
        BuiltinSkillIdView::DataExtraction,
        BuiltinSkillIdView::FormAssistant,
        BuiltinSkillIdView::Shopping,
        BuiltinSkillIdView::DownloadOrganizer,
        BuiltinSkillIdView::TravelResearch,
        BuiltinSkillIdView::VideoTranscriptAnalyzer,
        BuiltinSkillIdView::ImageUnderstanding,
        BuiltinSkillIdView::LibraryBuilder,
        BuiltinSkillIdView::SpreadsheetBuilder,
        BuiltinSkillIdView::DocumentGenerator,
    ];
    let required = [12, 14, 13, 8, 7, 7, 7, 12, 12, 9, 14, 9, 6, 6, 6, 8];
    let available = [12, 14, 13, 8, 7, 7, 7, 12, 12, 5, 14, 9, 6, 6, 6, 8];
    abilities
        .into_iter()
        .zip(ids)
        .zip(required)
        .zip(available)
        .enumerate()
        .map(
            |(index, (((ability, skill_id), required_tool_count), available_tool_count))| {
                BuiltinSkillView {
                    reference: BuiltinSkillReferenceView {
                        skill_id,
                        version: 1,
                    },
                    required_ability: ability,
                    enabled: !disabled.contains(&ability),
                    availability: if index == 9 {
                        BuiltinSkillAvailabilityView::RequiredToolUnavailable
                    } else {
                        BuiltinSkillAvailabilityView::Available
                    },
                    required_tool_count,
                    available_tool_count,
                    required_part_count: 0,
                    installed_part_count: 0,
                }
            },
        )
        .collect()
}

pub(super) fn golden_payload() -> Vec<u8> {
    let value = include_str!("../../../../golden/full-status-v1.hex").trim();
    value
        .as_bytes()
        .chunks_exact(2)
        .map(|pair| {
            let text = core::str::from_utf8(pair).unwrap_or_default();
            u8::from_str_radix(text, 16).unwrap_or_default()
        })
        .collect()
}

pub(super) fn empty_status() -> CoreStatus {
    CoreStatus {
        availability: CoreAvailability::Ready,
        generation: 1,
        active_tasks: Vec::new(),
        auth_state: None,
        workspaces: Vec::new(),
        workspace_export: None,
        asset_delivery: None,
        provider_roster: Vec::new(),
        provider_probes: Vec::new(),
        provider_models: Vec::new(),
        assistant_configuration: AssistantConfigurationView {
            revision: 0,
            disabled_abilities: Vec::new(),
            preset: PersonalityPresetView::CarefulResearcher,
            pace: 1,
            length: 1,
            check_in: 1,
        },
        library: LibraryViewState {
            availability: LibraryAvailability::Available,
            revision: 0,
            entries: Vec::new(),
            search: None,
            refresh_previews: Vec::new(),
            refresh_results: Vec::new(),
        },
        library_export: None,
        memory: MemoryViewState {
            availability: MemoryAvailability::Available,
            revision: 0,
            records: Vec::new(),
            search: None,
        },
        saved_sign_ins: SavedSignInsView {
            availability: SavedDataAvailability::Loading,
            revision: 0,
            records: Vec::new(),
        },
        saved_details: SavedDetailsView {
            availability: SavedDataAvailability::Loading,
            revision: 0,
            people: Vec::new(),
        },
        site_skills: Vec::new(),
        builtin_skills: builtin_skills_fixture(&[]),
        projection_mode: CoreStatusProjectionMode::Complete,
        projection_omissions: Vec::new(),
    }
}
