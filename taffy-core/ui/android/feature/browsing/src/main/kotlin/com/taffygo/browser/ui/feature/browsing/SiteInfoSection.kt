// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Connection, permissions, and desktop site — the below-the-fold half of
 * screen SCR-204. Blocking stays above; this is the rest of "on this site"
 * so those rows never grow the overflow.
 */
@Composable
internal fun SiteInfoSection(
    state: SiteFilteringUiState,
    onIntent: (BrowserMainIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        TaffySectionHeader(title = taffyString(R.string.taffy_site_filtering_connection))
        TaffyGroupedCard(testTag = SITE_FILTERING_CONNECTION_TEST_TAG) {
            val connection = taffyString(
                if (state.isSecure) {
                    R.string.taffy_site_filtering_secure
                } else {
                    R.string.taffy_site_filtering_insecure
                },
            )
            SiteInfoRow(
                icon = TaffyIcon.LockSimple,
                title = connection,
                iconTintPositive = state.isSecure,
            )
        }
        TaffySectionHeader(title = taffyString(R.string.taffy_site_filtering_permissions))
        TaffyGroupedCard(testTag = SITE_FILTERING_PERMISSIONS_TEST_TAG) {
            SitePermissionRows(state, onIntent)
        }
        PageZoomSection(state = state.pageZoom, onIntent = onIntent)
        TaffyGroupedCard(testTag = SITE_FILTERING_DESKTOP_TEST_TAG) {
            val title = taffyString(R.string.taffy_site_filtering_desktop)
            val supporting = if (state.desktopSiteAvailable) {
                null
            } else {
                taffyString(R.string.taffy_site_filtering_desktop_unavailable)
            }
            val description = if (supporting == null) {
                title
            } else {
                taffyString(R.string.taffy_site_filtering_desktop_description, title, supporting)
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                    .clickable(
                        enabled = state.desktopSiteAvailable,
                        role = Role.Switch,
                        onClick = { onIntent(BrowserMainIntent.ToggleDesktopSite) },
                    )
                    .padding(
                        horizontal = TaffyTheme.spacing.screenMargin,
                        vertical = TaffyTheme.spacing.snug,
                    )
                    .semantics(mergeDescendants = true) { contentDescription = description },
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                TaffyGlyphFrame {
                    Icon(
                        imageVector = TaffyIcon.Monitor,
                        contentDescription = null,
                        tint = TaffyTheme.colors.textSecondary,
                        modifier = Modifier.size(GlyphSize),
                    )
                }
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
                ) {
                    Text(
                        text = title,
                        style = TaffyTheme.typography.body,
                        color = TaffyTheme.colors.textPrimary,
                    )
                    if (supporting != null) {
                        Text(
                            text = supporting,
                            style = TaffyTheme.typography.detail,
                            color = TaffyTheme.colors.textSecondary,
                        )
                    }
                }
                Switch(
                    checked = state.desktopSite,
                    onCheckedChange = null,
                    enabled = state.desktopSiteAvailable,
                )
            }
        }
    }
}

@Composable
private fun SitePermissionRows(
    state: SiteFilteringUiState,
    onIntent: (BrowserMainIntent) -> Unit,
) {
    val permissions = state.permissions
    when (permissions) {
        SiteInfoRepository.PermissionState.Loading -> SiteInfoRow(
            icon = TaffyIcon.LockSimple,
            title = taffyString(R.string.taffy_site_filtering_permissions_loading),
            testTag = SITE_PERMISSIONS_LOADING_TEST_TAG,
        )
        SiteInfoRepository.PermissionState.Unavailable -> SiteInfoRow(
            icon = TaffyIcon.LockSimple,
            title = taffyString(R.string.taffy_site_filtering_permissions_unavailable),
            testTag = SITE_PERMISSIONS_UNAVAILABLE_TEST_TAG,
        )
        SiteInfoRepository.PermissionState.Empty -> SiteInfoRow(
            icon = TaffyIcon.LockSimple,
            title = taffyString(R.string.taffy_site_filtering_permissions_empty),
            testTag = SITE_PERMISSIONS_EMPTY_TEST_TAG,
        )
        is SiteInfoRepository.PermissionState.Changed -> {
            SiteInfoRow(
                icon = TaffyIcon.LockSimple,
                title = taffyPlural(
                    R.plurals.taffy_site_filtering_permissions_changed,
                    permissions.changedCount,
                    permissions.changedCount,
                ),
                testTag = SITE_PERMISSIONS_CHANGED_TEST_TAG,
            )
            permissions.capabilities.forEach { capability ->
                TaffyGroupedCardDivider()
                SiteInfoRow(
                    icon = TaffyIcon.Gear,
                    title = taffyString(permissionCapabilityLabel(capability)),
                    testTag = "$SITE_PERMISSION_CAPABILITY_TEST_TAG_PREFIX${capability.name.lowercase()}",
                )
            }
            if (permissions.otherCount > 0) {
                TaffyGroupedCardDivider()
                SiteInfoRow(
                    icon = TaffyIcon.Gear,
                    title = taffyPlural(
                        R.plurals.taffy_site_filtering_permissions_other,
                        permissions.otherCount,
                        permissions.otherCount,
                    ),
                    testTag = SITE_PERMISSIONS_OTHER_TEST_TAG,
                )
            }
        }
    }

    if (state.permissionReset != SiteFilteringUiState.ActionProgress.IDLE) {
        TaffyGroupedCardDivider()
        val status = when (state.permissionReset) {
            SiteFilteringUiState.ActionProgress.RUNNING ->
                R.string.taffy_site_filtering_permissions_resetting
            SiteFilteringUiState.ActionProgress.SUCCEEDED ->
                R.string.taffy_site_filtering_permissions_reset_done
            SiteFilteringUiState.ActionProgress.FAILED ->
                R.string.taffy_site_filtering_permissions_reset_failed
            SiteFilteringUiState.ActionProgress.IDLE -> error("Handled above")
        }
        SiteInfoRow(
            icon = TaffyIcon.Gear,
            title = taffyString(status),
            testTag = SITE_PERMISSIONS_RESET_STATUS_TEST_TAG,
        )
    }

    if (permissions is SiteInfoRepository.PermissionState.Changed) {
        TaffyGroupedCardDivider()
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_site_filtering_permissions_reset),
            onClick = { onIntent(BrowserMainIntent.RequestSitePermissionReset) },
            enabled = state.canResetPermissions,
            loading = state.permissionReset == SiteFilteringUiState.ActionProgress.RUNNING,
            testTag = SITE_PERMISSIONS_RESET_TEST_TAG,
            modifier = Modifier
                .fillMaxWidth()
                .padding(TaffyTheme.spacing.snug),
        )
    }
}

private fun permissionCapabilityLabel(
    capability: SiteInfoRepository.PermissionCapability,
): Int = when (capability) {
    SiteInfoRepository.PermissionCapability.LOCATION ->
        R.string.taffy_site_filtering_permission_location
    SiteInfoRepository.PermissionCapability.CAMERA ->
        R.string.taffy_site_filtering_permission_camera
    SiteInfoRepository.PermissionCapability.MICROPHONE ->
        R.string.taffy_site_filtering_permission_microphone
    SiteInfoRepository.PermissionCapability.NOTIFICATIONS ->
        R.string.taffy_site_filtering_permission_notifications
    SiteInfoRepository.PermissionCapability.JAVASCRIPT ->
        R.string.taffy_site_filtering_permission_javascript
    SiteInfoRepository.PermissionCapability.POP_UPS ->
        R.string.taffy_site_filtering_permission_pop_ups
    SiteInfoRepository.PermissionCapability.AUTOMATIC_DOWNLOADS ->
        R.string.taffy_site_filtering_permission_automatic_downloads
    SiteInfoRepository.PermissionCapability.CLIPBOARD ->
        R.string.taffy_site_filtering_permission_clipboard
    SiteInfoRepository.PermissionCapability.SENSORS ->
        R.string.taffy_site_filtering_permission_sensors
    SiteInfoRepository.PermissionCapability.SOUND ->
        R.string.taffy_site_filtering_permission_sound
}

@Composable
private fun SiteInfoRow(
    icon: ImageVector,
    title: String,
    iconTintPositive: Boolean = false,
    testTag: String? = null,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            )
            .then(if (testTag == null) Modifier else Modifier.testTag(testTag))
            .semantics(mergeDescendants = true) { contentDescription = title },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TaffyGlyphFrame {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = if (iconTintPositive) {
                    TaffyTheme.colors.positive
                } else {
                    TaffyTheme.colors.textSecondary
                },
                modifier = Modifier.size(GlyphSize),
            )
        }
        Text(
            text = title,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
    }
}

private val GlyphSize = 20.dp

/** The connection row on screen SCR-204. */
const val SITE_FILTERING_CONNECTION_TEST_TAG: String = "site_filtering_connection"

/** The permissions section on screen SCR-204. */
const val SITE_FILTERING_PERMISSIONS_TEST_TAG: String = "site_filtering_permissions"

const val SITE_PERMISSIONS_LOADING_TEST_TAG: String = "site_permissions_loading"
const val SITE_PERMISSIONS_UNAVAILABLE_TEST_TAG: String = "site_permissions_unavailable"
const val SITE_PERMISSIONS_EMPTY_TEST_TAG: String = "site_permissions_empty"
const val SITE_PERMISSIONS_CHANGED_TEST_TAG: String = "site_permissions_changed"
const val SITE_PERMISSION_CAPABILITY_TEST_TAG_PREFIX: String = "site_permission_capability_"
const val SITE_PERMISSIONS_OTHER_TEST_TAG: String = "site_permissions_other"
const val SITE_PERMISSIONS_RESET_TEST_TAG: String = "site_permissions_reset"
const val SITE_PERMISSIONS_RESET_STATUS_TEST_TAG: String = "site_permissions_reset_status"

/** The desktop-site row on screen SCR-204. */
const val SITE_FILTERING_DESKTOP_TEST_TAG: String = "site_filtering_desktop"
