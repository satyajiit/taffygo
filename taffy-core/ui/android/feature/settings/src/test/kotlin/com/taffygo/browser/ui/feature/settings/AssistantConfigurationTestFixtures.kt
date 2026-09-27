// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import kotlinx.coroutines.CoroutineDispatcher
import taffy.core_api.AssistantAbilityView
import taffy.core_api.AssistantConfigurationView
import taffy.core_api.BuiltinSkillAvailabilityView
import taffy.core_api.BuiltinSkillIdView
import taffy.core_api.BuiltinSkillReferenceView
import taffy.core_api.BuiltinSkillView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.PersonalityPresetView
import taffy.core_api.SiteSkillClauseKind
import taffy.core_api.SiteSkillObservedClause
import taffy.core_api.SiteSkillObservedStep
import taffy.core_api.SiteSkillProvenanceView
import taffy.core_api.SiteSkillStatusView
import taffy.core_api.SiteSkillView

internal fun assistantStatus(
    availability: CoreAvailability = CoreAvailability.READY,
    revision: ULong = 1uL,
    disabled: List<AssistantAbilityView> = emptyList(),
    preset: PersonalityPresetView = PersonalityPresetView.CAREFUL_RESEARCHER,
    scales: PersonalityRepository.Scales = PersonalityRepository.Preset.CAREFUL_RESEARCHER.scales(),
    siteSkills: List<SiteSkillView> = emptyList(),
    projectionMode: CoreStatusProjectionMode = CoreStatusProjectionMode.COMPLETE,
) = CoreStatus(
    availability = availability,
    generation = 3uL,
    active_tasks = emptyList(),
    auth_state = null,
    workspaces = emptyList(),
    workspace_export = null,
    asset_delivery = null,
    provider_roster = emptyList(),
    provider_probes = emptyList(),
    provider_models = emptyList(),
    assistant_configuration = AssistantConfigurationView(
        revision = revision,
        disabled_abilities = disabled,
        preset = preset,
        pace = scales.pace.toUInt(),
        length = scales.length.toUInt(),
        check_in = scales.checkIn.toUInt(),
    ),
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
    site_skills = siteSkills,
    builtin_skills = assistantBuiltinSkills(disabled),
    projection_mode = projectionMode,
    projection_omissions = emptyList(),
)

private fun assistantBuiltinSkills(
    disabled: List<AssistantAbilityView>,
): List<BuiltinSkillView> = SkillsRepository.previewBuiltIns().map { skill ->
    val skillId = requireNotNull(skill.id.toBuiltinSkillId())
    val ability = requireNotNull(skill.id.toAssistantAbility())
    val available = skillId != BuiltinSkillIdView.DOWNLOAD_ORGANIZER
    BuiltinSkillView(
        reference = BuiltinSkillReferenceView(skill_id = skillId, version = 1u),
        required_ability = ability,
        enabled = ability !in disabled,
        availability = if (available) {
            BuiltinSkillAvailabilityView.AVAILABLE
        } else {
            BuiltinSkillAvailabilityView.REQUIRED_TOOL_UNAVAILABLE
        },
        required_tool_count = 1u,
        available_tool_count = if (available) 1u else 0u,
        required_part_count = 0u,
        installed_part_count = 0u,
    )
}

internal fun assistantSiteSkill(
    status: SiteSkillStatusView = SiteSkillStatusView.DRAFT,
) = SiteSkillView(
    skill_id = "repeat-checkout",
    origin = "https://shop.example",
    provenance = SiteSkillProvenanceView.RECORDED_FROM_TASK,
    status = status,
    active_version = 4u,
    step_count = 2u,
    installed_at_epoch_ms = 1_780_000_000_000uL,
    updated_at_epoch_ms = 1_780_000_100_000uL,
    recorded_from_task_id = null,
    reviewed_steps = emptyList(),
)

internal fun assistantObservedRecording() = SkillsRepository.ObservedSiteRecording(
    origin = "https://shop.example",
    clauses = listOf(
        SiteSkillObservedClause(
            kind = SiteSkillClauseKind.ROLE_PRESENT,
            role = 0u,
            detail = 0u,
        ),
    ),
    steps = listOf(
        SiteSkillObservedStep(
            verb = "browser.tabs.list",
            arguments = emptyList(),
            postcondition = 0u,
            has_fill = false,
            fill_purpose = 0u,
        ),
    ),
)

internal fun assistantLifetime(dispatcher: CoroutineDispatcher) = TaffyProfileLifetime(
    dispatchers = object : AppDispatchers {
        override val main = dispatcher
        override val default = dispatcher
        override val io = dispatcher
    },
    failureSink = CoroutineFailureSink { },
)
