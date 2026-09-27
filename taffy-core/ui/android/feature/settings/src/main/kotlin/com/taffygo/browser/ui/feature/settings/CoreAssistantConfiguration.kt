// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.api.CoreApiClient
import taffy.core_api.AssistantAbilityView
import taffy.core_api.AssistantConfigurationView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.PersonalityPresetView

/** The complete published record used by both settings surfaces. */
internal fun CoreStatus.readyAssistantConfiguration(): AssistantConfigurationView? =
    assistant_configuration.takeIf { availability == CoreAvailability.READY }

/**
 * Submit one whole-record compare-and-swap and wait for publication to change
 * what either screen renders.
 */
internal suspend fun CoreApiClient.replaceAssistantConfiguration(
    current: AssistantConfigurationView,
    disabledAbilities: List<AssistantAbilityView> = current.disabled_abilities,
    preset: PersonalityPresetView = current.preset,
    scales: PersonalityRepository.Scales = current.toPersonalityScales(),
) {
    val normalizedAbilities = disabledAbilities.distinct().sortedBy { it.wire }
    if (
        current.disabled_abilities == normalizedAbilities &&
        current.preset == preset &&
        current.pace == scales.pace.toUInt() &&
        current.length == scales.length.toUInt() &&
        current.check_in == scales.checkIn.toUInt()
    ) {
        return
    }
    setAssistantConfiguration(
        expectedRevision = current.revision,
        disabledAbilities = normalizedAbilities,
        preset = preset,
        pace = scales.pace.toUInt(),
        length = scales.length.toUInt(),
        checkIn = scales.checkIn.toUInt(),
    )
}

internal fun AssistantConfigurationView.toPersonalityScales(): PersonalityRepository.Scales =
    PersonalityRepository.Scales(
        pace = pace.toInt(),
        length = length.toInt(),
        checkIn = check_in.toInt(),
    )
