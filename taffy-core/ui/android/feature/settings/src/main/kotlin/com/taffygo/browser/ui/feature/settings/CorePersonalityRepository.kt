// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.PersonalityPresetView

/** Window-facing projection of the profile's durable presentation choices. */
internal class CorePersonalityRepository(
    private val core: CoreApiClient,
    lifetime: TaffyProfileLifetime,
) : PersonalityRepository {

    override val snapshot: StateFlow<PersonalityRepository.Snapshot> = core.status
        .distinctUntilChangedBy(CoreStatus::personalityProjectionVersion)
        .map(CoreStatus::toPersonalitySnapshot)
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.toPersonalitySnapshot(),
        )

    override suspend fun choosePreset(preset: PersonalityRepository.Preset) {
        val current = core.status.value.readyAssistantConfiguration() ?: return
        core.replaceAssistantConfiguration(
            current = current,
            preset = preset.toCorePreset(),
            scales = preset.scales(),
        )
    }

    override suspend fun setScales(scales: PersonalityRepository.Scales) {
        val current = core.status.value.readyAssistantConfiguration() ?: return
        core.replaceAssistantConfiguration(current, scales = scales.clamped())
    }
}

internal data class PersonalityProjectionVersion(
    val availability: CoreAvailability,
    val preset: PersonalityPresetView,
    val pace: UInt,
    val length: UInt,
    val checkIn: UInt,
)

internal fun CoreStatus.personalityProjectionVersion() = PersonalityProjectionVersion(
    availability = availability,
    preset = assistant_configuration.preset,
    pace = assistant_configuration.pace,
    length = assistant_configuration.length,
    checkIn = assistant_configuration.check_in,
)

private fun CoreStatus.toPersonalitySnapshot(): PersonalityRepository.Snapshot = when (availability) {
    CoreAvailability.STARTING ->
        PersonalityRepository.Snapshot(PersonalityRepository.Availability.LOADING)
    CoreAvailability.READY -> PersonalityRepository.Snapshot(
        availability = PersonalityRepository.Availability.READY,
        selected = assistant_configuration.preset.toUiPreset(),
        scales = assistant_configuration.toPersonalityScales(),
    )
    CoreAvailability.UNAVAILABLE,
    CoreAvailability.CIRCUIT_OPEN,
    -> PersonalityRepository.Snapshot(PersonalityRepository.Availability.UNAVAILABLE)
}

private fun PersonalityRepository.Preset.toCorePreset(): PersonalityPresetView = when (this) {
    PersonalityRepository.Preset.CAREFUL_RESEARCHER -> PersonalityPresetView.CAREFUL_RESEARCHER
    PersonalityRepository.Preset.QUICK_SHOPPER -> PersonalityPresetView.QUICK_SHOPPER
    PersonalityRepository.Preset.TRIP_PLANNER -> PersonalityPresetView.TRIP_PLANNER
}

private fun PersonalityPresetView.toUiPreset(): PersonalityRepository.Preset = when (this) {
    PersonalityPresetView.CAREFUL_RESEARCHER -> PersonalityRepository.Preset.CAREFUL_RESEARCHER
    PersonalityPresetView.QUICK_SHOPPER -> PersonalityRepository.Preset.QUICK_SHOPPER
    PersonalityPresetView.TRIP_PLANNER -> PersonalityRepository.Preset.TRIP_PLANNER
}
