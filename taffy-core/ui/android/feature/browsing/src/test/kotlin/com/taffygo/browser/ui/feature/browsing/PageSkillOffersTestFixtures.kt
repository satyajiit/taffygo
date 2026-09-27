// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import taffy.core_api.SiteSkillArgumentKind
import taffy.core_api.SiteSkillObservedArgument
import taffy.core_api.SiteSkillObservedStep
import taffy.core_api.SiteSkillProvenanceView
import taffy.core_api.SiteSkillStatusView
import taffy.core_api.SiteSkillView

internal const val FLOW_TEST_ADDRESS = "https://identity.example.test/download"

internal fun pageFlowSkill() = SiteSkillView(
    skill_id = "download-document", origin = "https://identity.example.test",
    provenance = SiteSkillProvenanceView.RECORDED_FROM_TASK, status = SiteSkillStatusView.ACTIVE,
    active_version = 1u, step_count = 1u, installed_at_epoch_ms = 1uL, updated_at_epoch_ms = 1uL,
    recorded_from_task_id = "finished", reviewed_steps = listOf(SiteSkillObservedStep(
        verb = "browser.navigate", arguments = listOf(SiteSkillObservedArgument(
            parameter = 0u, kind = SiteSkillArgumentKind.PUBLIC_ADDRESS, value = 0uL, purpose = 0u,
            public_address = FLOW_TEST_ADDRESS, semantic_target = null,
        )), postcondition = 0u, has_fill = false, fill_purpose = 0u,
    )),
)

internal fun pageFlowStatus(skills: List<SiteSkillView> = listOf(pageFlowSkill())) = taffy.core_api.CoreStatus(
            availability = taffy.core_api.CoreAvailability.READY,
            generation = 1uL,
            active_tasks = emptyList(),
            auth_state = null,
            workspaces = emptyList(),
            workspace_export = null,
            asset_delivery = null,
            provider_roster = emptyList(),
            provider_probes = emptyList(),
            provider_models = emptyList(),
            library = taffy.core_api.LibraryViewState(
                availability = taffy.core_api.LibraryAvailability.AVAILABLE,
                revision = 0uL,
                entries = emptyList(),
                search = null,
                refresh_previews = emptyList(),
                refresh_results = emptyList(),
            ),
            library_export = null,
            memory = taffy.core_api.MemoryViewState(
                availability = taffy.core_api.MemoryAvailability.AVAILABLE,
                revision = 0uL,
                records = emptyList(),
                search = null,
            ),
            assistant_configuration = taffy.core_api.AssistantConfigurationView(
                revision = 0uL,
                disabled_abilities = emptyList(),
                preset = taffy.core_api.PersonalityPresetView.CAREFUL_RESEARCHER,
                pace = 0u,
                length = 1u,
                check_in = 0u,
            ),
            saved_sign_ins = taffy.core_api.SavedSignInsView(
                availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
                revision = 0uL,
                records = emptyList(),
            ),
            saved_details = taffy.core_api.SavedDetailsView(
                availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
                revision = 0uL,
                people = emptyList(),
            ),
            site_skills = skills,
            builtin_skills = emptyList(),
            projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
            projection_omissions = emptyList(),
        )
