// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySectionBar
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-205 — site settings. */
@Composable
fun SiteSettingsScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: SiteSettingsViewModel = screenViewModel(TaffyDestination.SiteSettings)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    SiteSettingsContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) {
            ({ viewModel.onIntent(SiteSettingsIntent.Dismiss, navigator) })
        } else {
            null
        },
        modifier = modifier,
    )
}

/** Live defaults and exceptions; loading and unavailable never invent either. */
@Composable
fun SiteSettingsContent(
    state: SiteSettingsUiState,
    onIntent: (SiteSettingsIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyLazyScreen(
        destination = TaffyDestination.SiteSettings,
        title = taffyString(R.string.taffy_site_settings_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = if (state.sites.isNotEmpty()) {
            Modifier.testTag(SITE_SETTINGS_LIST_TEST_TAG)
        } else {
            Modifier
        },
    ) {
        when {
            state.loading -> item(contentType = "loading") { SiteSettingsLoading() }
            !state.available -> item(contentType = "unavailable") {
                SiteSettingsUnavailable()
            }
            else -> {
                item(contentType = "section") {
                    TaffySectionBar(title = taffyString(R.string.taffy_site_settings_defaults))
                }
                if (state.defaults.isEmpty()) {
                    item(contentType = "defaults-unavailable") {
                        TaffyInfoTile(testTag = SITE_SETTINGS_DEFAULTS_UNAVAILABLE_TEST_TAG) {
                            Text(
                                text = taffyString(
                                    R.string.taffy_site_settings_defaults_unavailable,
                                ),
                                style = TaffyTheme.typography.detail,
                                color = TaffyTheme.colors.textSecondary,
                            )
                        }
                    }
                } else {
                    item(contentType = "defaults") {
                        TaffyGroupedCard(testTag = SITE_SETTINGS_DEFAULTS_TEST_TAG) {
                            state.defaults.forEachIndexed { index, setting ->
                                SiteDefaultRow(
                                    setting = setting,
                                    updating = state.updating,
                                    onSet = { enabled ->
                                        onIntent(
                                            SiteSettingsIntent.SetDefault(
                                                setting.capability,
                                                enabled,
                                            ),
                                        )
                                    },
                                )
                                if (index < state.defaults.lastIndex) TaffyGroupedCardDivider()
                            }
                        }
                    }
                }
                if (state.failedUpdate != null) {
                    item(contentType = "update-failed") {
                        TaffyInfoTile(testTag = SITE_SETTINGS_UPDATE_FAILED_TEST_TAG) {
                            Text(
                                text = taffyString(R.string.taffy_site_settings_update_failed),
                                style = TaffyTheme.typography.detail,
                                color = TaffyTheme.colors.textSecondary,
                            )
                        }
                    }
                }
                item(contentType = "section") {
                    TaffySectionBar(title = taffyString(R.string.taffy_site_settings_exceptions))
                }
                state.resettingHost?.let { host ->
                    item(key = "resetting-$host", contentType = "resetting") {
                        SiteResetStatus(
                            text = taffyString(R.string.taffy_site_settings_resetting, host),
                            testTag = SITE_SETTINGS_RESETTING_TEST_TAG,
                        )
                    }
                }
                state.failedResetHost?.let { host ->
                    item(key = "reset-failed-$host", contentType = "reset-failed") {
                        SiteResetStatus(
                            text = taffyString(R.string.taffy_site_settings_reset_failed, host),
                            testTag = SITE_SETTINGS_RESET_FAILED_TEST_TAG,
                        )
                    }
                }
                if (state.sites.isEmpty()) {
                    item(contentType = "empty") {
                        TaffyEmptyState(
                            title = taffyString(R.string.taffy_site_settings_empty_title),
                            body = taffyString(R.string.taffy_site_settings_empty_body),
                            leading = { SiteSettingsEmptyGlyph() },
                        )
                    }
                } else {
                    items(
                        count = state.sites.size,
                        key = { index -> state.sites[index].host },
                        contentType = { "site" },
                    ) { index ->
                        val site = state.sites[index]
                        SiteException(
                            site = site,
                            enabled = state.updating == null && state.resettingHost == null,
                            onClick = {
                                onIntent(SiteSettingsIntent.RequestSiteReset(site.host))
                            },
                        )
                    }
                }
            }
        }
    }
    state.resetCandidate?.let { host ->
        SiteSettingsResetSheet(
            host = host,
            onConfirm = { onIntent(SiteSettingsIntent.ConfirmSiteReset) },
            onDismiss = { onIntent(SiteSettingsIntent.DismissSiteReset) },
        )
    }
}

@Composable
private fun SiteException(
    site: SiteSettingsUiState.Site,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    TaffyGroupedCard {
        SettingsHomeRow(
            title = site.host,
            summary = pluralStringResource(
                R.plurals.taffy_site_settings_changed_permissions,
                site.changedPermissionCount,
                site.changedPermissionCount,
            ),
            icon = TaffyIcon.Gear,
            testTag = "$SITE_SETTINGS_ROW_TEST_TAG_PREFIX${site.host}",
            selected = false,
            accentSelected = false,
            onClick = onClick,
            enabled = enabled,
            showChevron = true,
        )
    }
}

@Composable
private fun SiteResetStatus(text: String, testTag: String) {
    TaffyInfoTile(testTag = testTag) {
        Text(
            text = text,
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

@Composable
private fun SiteSettingsUnavailable() {
    TaffyInfoTile(testTag = SITE_SETTINGS_UNAVAILABLE_TEST_TAG) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Text(
                text = taffyString(R.string.taffy_site_settings_unavailable_title),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                text = taffyString(R.string.taffy_site_settings_unavailable_body),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

@Composable
private fun SiteSettingsLoading() {
    val description = taffyString(R.string.taffy_site_settings_loading)
    repeat(SITE_SETTINGS_SKELETON_COUNT) { index ->
        TaffySkeleton(
            modifier = Modifier
                .fillMaxWidth()
                .height(SkeletonHeight)
                .padding(vertical = TaffyTheme.spacing.tight)
                .then(
                    if (index == 0) Modifier.testTag(SITE_SETTINGS_LOADING_TEST_TAG) else Modifier,
                ),
            accessibleDescription = if (index == 0) description else null,
        )
    }
}

@Composable
private fun SiteSettingsEmptyGlyph() {
    Icon(
        imageVector = TaffyIcon.Gear,
        contentDescription = null,
        tint = TaffyTheme.colors.textPrimary,
        modifier = Modifier.size(SettingsGlyphSize),
    )
}

const val SITE_SETTINGS_LIST_TEST_TAG: String = "site_settings_list"
const val SITE_SETTINGS_DEFAULTS_TEST_TAG: String = "site_settings_defaults"
const val SITE_SETTINGS_DEFAULTS_UNAVAILABLE_TEST_TAG: String = "site_settings_defaults_unavailable"
const val SITE_SETTINGS_UPDATE_FAILED_TEST_TAG: String = "site_settings_update_failed"
const val SITE_SETTINGS_DEFAULT_ROW_TEST_TAG_PREFIX: String = "site_settings_default_"
const val SITE_SETTINGS_DEFAULT_SWITCH_TEST_TAG_PREFIX: String = "site_settings_default_switch_"
const val SITE_SETTINGS_LOADING_TEST_TAG: String = "site_settings_loading"
const val SITE_SETTINGS_UNAVAILABLE_TEST_TAG: String = "site_settings_unavailable"
const val SITE_SETTINGS_ROW_TEST_TAG_PREFIX: String = "site_settings_row_"
const val SITE_SETTINGS_RESET_SHEET_TEST_TAG: String = "site_settings_reset_sheet"
const val SITE_SETTINGS_RESET_CONFIRM_TEST_TAG: String = "site_settings_reset_confirm"
const val SITE_SETTINGS_RESETTING_TEST_TAG: String = "site_settings_resetting"
const val SITE_SETTINGS_RESET_FAILED_TEST_TAG: String = "site_settings_reset_failed"
private const val SITE_SETTINGS_SKELETON_COUNT = 4
private val SkeletonHeight = 56.dp
