// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.taffyString

/** One live Chromium content-setting default with policy and mutation state. */
@Composable
internal fun SiteDefaultRow(
    setting: SiteSettingsUiState.Default,
    updating: SiteSettingsRepository.Capability?,
    onSet: (Boolean) -> Unit,
) {
    val name = taffyString(capabilityName(setting.capability))
    val summary = when {
        !setting.userModifiable -> taffyString(R.string.taffy_site_settings_managed)
        setting.enabled -> taffyString(capabilityEnabledSummary(setting.capability))
        else -> taffyString(R.string.taffy_site_settings_blocked)
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(TaffyTheme.spacing.screenMargin)
            .testTag("$SITE_SETTINGS_DEFAULT_ROW_TEST_TAG_PREFIX${setting.capability.name}"),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(name, style = TaffyTheme.typography.body, color = TaffyTheme.colors.textPrimary)
            Text(
                summary,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        TaffySwitch(
            checked = setting.enabled,
            onCheckedChange = onSet,
            accessibleName = name,
            enabled = setting.userModifiable && updating == null,
            testTag = "$SITE_SETTINGS_DEFAULT_SWITCH_TEST_TAG_PREFIX${setting.capability.name}",
        )
    }
}

private fun capabilityName(capability: SiteSettingsRepository.Capability): Int = when (capability) {
    SiteSettingsRepository.Capability.LOCATION -> R.string.taffy_site_settings_location
    SiteSettingsRepository.Capability.CAMERA -> R.string.taffy_site_settings_camera
    SiteSettingsRepository.Capability.MICROPHONE -> R.string.taffy_site_settings_microphone
    SiteSettingsRepository.Capability.NOTIFICATIONS -> R.string.taffy_site_settings_notifications
    SiteSettingsRepository.Capability.JAVASCRIPT -> R.string.taffy_site_settings_javascript
    SiteSettingsRepository.Capability.POP_UPS -> R.string.taffy_site_settings_pop_ups
    SiteSettingsRepository.Capability.AUTOMATIC_DOWNLOADS ->
        R.string.taffy_site_settings_automatic_downloads
    SiteSettingsRepository.Capability.CLIPBOARD -> R.string.taffy_site_settings_clipboard
    SiteSettingsRepository.Capability.SENSORS -> R.string.taffy_site_settings_sensors
    SiteSettingsRepository.Capability.SOUND -> R.string.taffy_site_settings_sound
}

private fun capabilityEnabledSummary(capability: SiteSettingsRepository.Capability): Int =
    when (capability) {
        SiteSettingsRepository.Capability.LOCATION -> R.string.taffy_site_settings_location_ask
        SiteSettingsRepository.Capability.CAMERA -> R.string.taffy_site_settings_camera_ask
        SiteSettingsRepository.Capability.MICROPHONE -> R.string.taffy_site_settings_microphone_ask
        SiteSettingsRepository.Capability.NOTIFICATIONS ->
            R.string.taffy_site_settings_notifications_ask
        SiteSettingsRepository.Capability.AUTOMATIC_DOWNLOADS ->
            R.string.taffy_site_settings_downloads_ask
        SiteSettingsRepository.Capability.CLIPBOARD -> R.string.taffy_site_settings_clipboard_ask
        SiteSettingsRepository.Capability.SENSORS -> R.string.taffy_site_settings_sensors_ask
        SiteSettingsRepository.Capability.JAVASCRIPT,
        SiteSettingsRepository.Capability.POP_UPS,
        SiteSettingsRepository.Capability.SOUND,
        -> R.string.taffy_site_settings_allowed
    }
