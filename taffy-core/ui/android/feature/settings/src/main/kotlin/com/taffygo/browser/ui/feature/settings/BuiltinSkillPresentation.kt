// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import taffy.core_api.AssistantAbilityView
import taffy.core_api.BuiltinSkillAvailabilityView
import taffy.core_api.BuiltinSkillIdView
import taffy.core_api.BuiltinSkillView

private data class BuiltinPresentation(
    val id: String,
    val ability: AssistantAbilityView,
    val group: SkillsRepository.Group,
    val mayUse: List<SkillsRepository.MayUse>,
)

internal fun List<BuiltinSkillView>.toSkillsOrNull(): List<SkillsRepository.Skill>? {
    if (size != BuiltinSkillIdView.entries.size) return null
    return zip(BuiltinSkillIdView.entries).map { (view, expectedId) ->
        if (view.reference.skill_id != expectedId) return null
        val presentation = expectedId.presentation()
        if (view.required_ability != presentation.ability) return null
        SkillsRepository.Skill(
            id = presentation.id,
            group = presentation.group,
            mayUse = presentation.mayUse,
            enabled = view.enabled,
            builtIn = true,
            readiness = view.availability.toReadiness(),
            version = view.reference.version,
        )
    }
}

private fun BuiltinSkillAvailabilityView.toReadiness(): SkillsRepository.Readiness = when (this) {
    BuiltinSkillAvailabilityView.AVAILABLE -> SkillsRepository.Readiness.READY
    BuiltinSkillAvailabilityView.REQUIRED_TOOL_UNAVAILABLE ->
        SkillsRepository.Readiness.REQUIRED_TOOL_UNAVAILABLE
    BuiltinSkillAvailabilityView.REQUIRED_PART_MISSING ->
        SkillsRepository.Readiness.REQUIRED_PART_MISSING
    BuiltinSkillAvailabilityView.PROFILE_UNAVAILABLE ->
        SkillsRepository.Readiness.PROFILE_UNAVAILABLE
    BuiltinSkillAvailabilityView.POLICY_UNAVAILABLE ->
        SkillsRepository.Readiness.POLICY_UNAVAILABLE
}

internal fun String.toBuiltinSkillId(): BuiltinSkillIdView? =
    BuiltinSkillIdView.entries.firstOrNull { it.presentation().id == this }

internal fun String.toAssistantAbility(): AssistantAbilityView? =
    toBuiltinSkillId()?.presentation()?.ability

private fun BuiltinSkillIdView.presentation(): BuiltinPresentation = when (this) {
    BuiltinSkillIdView.GENERAL_WEB_RESEARCH -> presentation(
        SkillsRepository.GENERAL_WEB_RESEARCH,
        AssistantAbilityView.PAGES_LOOKUP,
        SkillsRepository.Group.PAGES,
        SkillsRepository.MayUse.PAGES,
    )
    BuiltinSkillIdView.DEEP_RESEARCH -> presentation(
        SkillsRepository.DEEP_RESEARCH,
        AssistantAbilityView.DEPTH,
        SkillsRepository.Group.RESEARCH,
        SkillsRepository.MayUse.PAGES,
    )
    BuiltinSkillIdView.PRODUCT_COMPARISON -> presentation(
        SkillsRepository.PRODUCT_COMPARISON,
        AssistantAbilityView.PRODUCTS,
        SkillsRepository.Group.SHOPPING,
        SkillsRepository.MayUse.PAGES,
    )
    BuiltinSkillIdView.MULTI_TAB_COMPARISON -> presentation(
        SkillsRepository.MULTI_TAB_COMPARISON,
        AssistantAbilityView.PAGES_COMPARE,
        SkillsRepository.Group.PAGES,
        SkillsRepository.MayUse.PAGES,
    )
    BuiltinSkillIdView.WEBSITE_SUMMARIZER -> presentation(
        SkillsRepository.WEBSITE_SUMMARIZER,
        AssistantAbilityView.PAGES_SUMMARIZE,
        SkillsRepository.Group.PAGES,
        SkillsRepository.MayUse.PAGES,
    )
    BuiltinSkillIdView.PDF_ANALYSIS -> presentation(
        SkillsRepository.PDF_ANALYSIS,
        AssistantAbilityView.PDF,
        SkillsRepository.Group.MEDIA,
        SkillsRepository.MayUse.FILES,
    )
    BuiltinSkillIdView.DATA_EXTRACTION -> presentation(
        SkillsRepository.DATA_EXTRACTION,
        AssistantAbilityView.PAGES_TABLE,
        SkillsRepository.Group.PAGES,
        SkillsRepository.MayUse.PAGES,
    )
    BuiltinSkillIdView.FORM_ASSISTANT -> presentation(
        SkillsRepository.FORM_ASSISTANT,
        AssistantAbilityView.FORM,
        SkillsRepository.Group.FORMS,
        SkillsRepository.MayUse.FORM,
    )
    BuiltinSkillIdView.SHOPPING -> presentation(
        SkillsRepository.SHOPPING,
        AssistantAbilityView.OFFERS,
        SkillsRepository.Group.SHOPPING,
        SkillsRepository.MayUse.PAGES,
    )
    BuiltinSkillIdView.DOWNLOAD_ORGANIZER -> presentation(
        SkillsRepository.DOWNLOAD_ORGANIZER,
        AssistantAbilityView.DOWNLOADS,
        SkillsRepository.Group.FORMS,
        SkillsRepository.MayUse.DOWNLOADS,
    )
    BuiltinSkillIdView.TRAVEL_RESEARCH -> presentation(
        SkillsRepository.TRAVEL_RESEARCH,
        AssistantAbilityView.TRIP,
        SkillsRepository.Group.RESEARCH,
        SkillsRepository.MayUse.PAGES,
    )
    BuiltinSkillIdView.VIDEO_TRANSCRIPT_ANALYZER -> presentation(
        SkillsRepository.VIDEO_TRANSCRIPT_ANALYZER,
        AssistantAbilityView.VIDEO,
        SkillsRepository.Group.MEDIA,
        SkillsRepository.MayUse.VIDEO,
    )
    BuiltinSkillIdView.IMAGE_UNDERSTANDING -> presentation(
        SkillsRepository.IMAGE_UNDERSTANDING,
        AssistantAbilityView.PICTURES,
        SkillsRepository.Group.MEDIA,
        SkillsRepository.MayUse.PICTURES,
    )
    BuiltinSkillIdView.LIBRARY_BUILDER -> presentation(
        SkillsRepository.LIBRARY_BUILDER,
        AssistantAbilityView.KEEP,
        SkillsRepository.Group.LIBRARY,
        SkillsRepository.MayUse.LIBRARY,
    )
    BuiltinSkillIdView.SPREADSHEET_BUILDER -> presentation(
        SkillsRepository.SPREADSHEET_BUILDER,
        AssistantAbilityView.SHEET,
        SkillsRepository.Group.FORMS,
        SkillsRepository.MayUse.FILES,
    )
    BuiltinSkillIdView.DOCUMENT_GENERATOR -> presentation(
        SkillsRepository.DOCUMENT_GENERATOR,
        AssistantAbilityView.DOCUMENT,
        SkillsRepository.Group.FORMS,
        SkillsRepository.MayUse.FILES,
    )
}

private fun presentation(
    id: String,
    ability: AssistantAbilityView,
    group: SkillsRepository.Group,
    mayUse: SkillsRepository.MayUse,
) = BuiltinPresentation(id, ability, group, listOf(mayUse))
