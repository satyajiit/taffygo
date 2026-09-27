// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyListRow
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffyStatTile
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-204 — the site sheet for Ads and trackers, over the page it is
 * about.
 *
 * Above the fold: the blocked count, the allow switch, and why this matters.
 * Below: connection, permissions, and desktop site. Ads and trackers settings
 * still opens SCR-206.
 */
@Composable
internal fun SiteFilteringSheet(
    state: SiteFilteringUiState,
    onIntent: (BrowserMainIntent) -> Unit,
) {
    if (state.permissionResetConfirmation) {
        PagesConfirmSheet(
            title = taffyString(R.string.taffy_site_filtering_permissions_reset_title),
            body = taffyString(
                R.string.taffy_site_filtering_permissions_reset_body,
                state.host,
            ),
            confirmLabel = taffyString(
                R.string.taffy_site_filtering_permissions_reset_confirm,
            ),
            cancelLabel = taffyString(R.string.taffy_site_filtering_permissions_reset_cancel),
            onConfirm = { onIntent(BrowserMainIntent.ConfirmSitePermissionReset) },
            onDismiss = { onIntent(BrowserMainIntent.DismissSitePermissionReset) },
            testTag = SITE_PERMISSIONS_RESET_CONFIRMATION_TEST_TAG,
        )
        return
    }
    TaffyBottomSheet(
        title = state.host.ifBlank { taffyString(R.string.taffy_site_filtering_title) },
        onDismissRequest = { onIntent(BrowserMainIntent.DismissSiteFiltering) },
        testTag = SITE_FILTERING_TEST_TAG,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            StatusLine(state)
            if (state.canToggleSite) {
                SiteToggleRow(state, onIntent)
            }
            if (state.siteBlocking == SiteFilteringUiState.ActionProgress.FAILED) {
                // Only the refusal is drawn. A change that was recorded has
                // already moved the switch above, and saying so again is noise.
                TaffyInfoTile(testTag = SITE_FILTERING_SITE_FAILED_TEST_TAG) {
                    Text(
                        text = taffyString(R.string.taffy_site_filtering_site_failed),
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textSecondary,
                    )
                }
            }
            WhyThisMatters()
            SiteInfoSection(state = state, onIntent = onIntent)
            TaffyListRow(
                title = taffyString(R.string.taffy_site_filtering_settings),
                accessibleDescription = taffyString(R.string.taffy_site_filtering_settings),
                onClick = { onIntent(BrowserMainIntent.OpenFilteringSettings) },
                testTag = SITE_FILTERING_SETTINGS_TEST_TAG,
            )
        }
    }
}

@Composable
private fun StatusLine(state: SiteFilteringUiState) {
    when {
        !state.enabled -> StatusCard(
            text = taffyString(R.string.taffy_site_filtering_master_off),
            active = false,
        )
        state.excepted -> StatusCard(
            text = taffyString(R.string.taffy_site_filtering_site_off),
            active = false,
        )
        state.blockedCount > 0 -> {
            val spoken = taffyPlural(
                R.plurals.taffy_site_filtering_blocked,
                state.blockedCount,
                state.blockedCount,
            )
            Box(
                modifier = Modifier
                    .testTag(SITE_FILTERING_STATUS_TEST_TAG)
                    .semantics(mergeDescendants = true) {
                        contentDescription = spoken
                    },
            ) {
                TaffyStatTile(
                    value = taffyCount(state.blockedCount),
                    label = taffyString(R.string.taffy_site_filtering_page_stat),
                    icon = TaffyIcon.Funnel,
                )
            }
        }
        else -> StatusCard(
            text = taffyString(R.string.taffy_site_filtering_none_yet),
            active = state.filteringActive,
        )
    }
}

@Composable
private fun StatusCard(text: String, active: Boolean) {
    TaffyObjectCard {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Icon(
                imageVector = TaffyIcon.ShieldCheck,
                contentDescription = null,
                modifier = Modifier.size(StatusShieldSize),
                tint = if (active) {
                    TaffyTheme.colors.positive
                } else {
                    TaffyTheme.colors.textSecondary
                },
            )
            Text(
                text = text,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.testTag(SITE_FILTERING_STATUS_TEST_TAG),
            )
        }
    }
}

@Composable
private fun SiteToggleRow(
    state: SiteFilteringUiState,
    onIntent: (BrowserMainIntent) -> Unit,
) {
    val blockingHere = !state.excepted
    val name = taffyString(R.string.taffy_site_filtering_site_toggle)
    TaffyObjectCard {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            ) {
                Text(
                    text = name,
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = state.host,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            TaffySwitch(
                checked = blockingHere,
                onCheckedChange = { onIntent(BrowserMainIntent.SetSiteBlocking(it)) },
                accessibleName = name,
                testTag = SITE_FILTERING_TOGGLE_TEST_TAG,
            )
        }
    }
}

@Composable
private fun WhyThisMatters() {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        Text(
            text = taffyString(R.string.taffy_site_filtering_why_heading),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier
                .testTag(SITE_FILTERING_WHY_TEST_TAG)
                .semantics { heading() },
        )
        TaffyInfoTile {
            Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
                Text(
                    text = taffyString(R.string.taffy_site_filtering_why_body),
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = taffyString(R.string.taffy_site_filtering_breakage),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
}

private val StatusShieldSize = 16.dp

/** The sheet, which screen SCR-204's semantics tests name. */
const val SITE_FILTERING_TEST_TAG: String = "site_filtering_sheet"

/** The status sentence inside it. */
const val SITE_FILTERING_STATUS_TEST_TAG: String = "site_filtering_status"

/** The per-site toggle row. */
const val SITE_FILTERING_TOGGLE_TEST_TAG: String = "site_filtering_toggle"

/** The refusal shown when the browser would not record the site's change. */
const val SITE_FILTERING_SITE_FAILED_TEST_TAG: String = "site_filtering_site_failed"

/** The row that opens screen SCR-206. */
const val SITE_FILTERING_SETTINGS_TEST_TAG: String = "site_filtering_settings"

/** Why this matters. */
const val SITE_FILTERING_WHY_TEST_TAG: String = "site_filtering_why"

/** Confirmation boundary before SCR-204 removes saved permission choices. */
const val SITE_PERMISSIONS_RESET_CONFIRMATION_TEST_TAG: String =
    "site_permissions_reset_confirmation"
