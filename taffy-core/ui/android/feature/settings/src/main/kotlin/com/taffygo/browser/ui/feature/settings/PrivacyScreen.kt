// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyHeroCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-403 — Privacy. */
@Composable
fun PrivacyScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: PrivacyViewModel = screenViewModel(TaffyDestination.Privacy)
    val state by viewModel.state.collectAsStateWithLifecycle()
    val context = LocalContext.current.applicationContext
    val ioDispatcher = LocalAppDispatchers.current.io
    val writer = remember(context, ioDispatcher) {
        ProfileDataExportDocumentWriter(context.contentResolver, ioDispatcher)
    }
    val createDocument = rememberLauncherForActivityResult(CreateProfileDataExportDocument()) { uri ->
        if (uri == null) {
            viewModel.onIntent(PrivacyIntent.ExportDestinationCancelled, navigator)
        } else {
            viewModel.writeExport { content -> writer.write(uri, content) }
        }
    }
    LaunchedEffect(Unit) { viewModel.onShown() }
    PrivacyContent(
        state = state,
        onIntent = { intent ->
            if (intent != PrivacyIntent.RequestExport) {
                viewModel.onIntent(intent, navigator)
            } else if (viewModel.state.value.canStartExport) {
                viewModel.onIntent(intent, navigator)
                try {
                    createDocument.launch(Unit)
                } catch (_: RuntimeException) {
                    viewModel.onIntent(PrivacyIntent.ExportDestinationFailed, navigator)
                }
            }
        },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/**
 * Honest copy, a grouped list, and whole-profile controls.
 *
 * No diagnostics switch: nothing read the one that stood here, and TaffyGo
 * has nowhere to send crash or usage facts (decision 0200).
 */
@Composable
fun PrivacyContent(
    state: PrivacyUiState,
    onIntent: (PrivacyIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.Privacy,
        title = taffyString(R.string.taffy_privacy_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        TaffyHeroCard(
            title = taffyString(R.string.taffy_privacy_title),
            body = taffyString(R.string.taffy_privacy_hero_body),
            illustration = painterResource(R.drawable.taffy_hero_privacy),
        )
        PrivacyCopyCard(
            heading = taffyString(R.string.taffy_privacy_stored_heading),
            body = taffyString(R.string.taffy_privacy_stored_body),
            icon = TaffyIcon.House,
            testTag = PRIVACY_STORED_TEST_TAG,
        ) {
            PrivacyInventory(state)
        }
        PrivacyCopyCard(
            heading = taffyString(R.string.taffy_privacy_route_heading),
            body = taffyString(privacyRouteBodyRes(state.route)),
            icon = TaffyIcon.Cloud,
            testTag = PRIVACY_ROUTE_TEST_TAG,
        )
        PrivacyCopyCard(
            heading = taffyString(R.string.taffy_privacy_retention_heading),
            body = taffyString(R.string.taffy_privacy_retention_body),
            icon = TaffyIcon.Clock,
            testTag = PRIVACY_RETENTION_TEST_TAG,
        )
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            SettingsHomeEyebrow(title = taffyString(R.string.taffy_privacy_manage_heading))
            TaffyGroupedCard(testTag = PRIVACY_LINKS_TEST_TAG) {
                SettingsHomeRow(
                    title = taffyString(R.string.taffy_privacy_site_settings),
                    summary = taffyString(R.string.taffy_privacy_site_settings_summary),
                    icon = TaffyIcon.Gear,
                    testTag = PRIVACY_SITE_SETTINGS_TEST_TAG,
                    selected = false,
                    accentSelected = false,
                    onClick = { onIntent(PrivacyIntent.OpenSiteSettings) },
                )
                TaffyGroupedCardDivider()
                SettingsHomeRow(
                    title = taffyString(R.string.taffy_privacy_clear),
                    summary = taffyString(R.string.taffy_privacy_clear_summary),
                    icon = TaffyIcon.Trash,
                    testTag = PRIVACY_CLEAR_TEST_TAG,
                    selected = false,
                    accentSelected = false,
                    onClick = { onIntent(PrivacyIntent.OpenClearData) },
                )
                TaffyGroupedCardDivider()
                SettingsHomeRow(
                    title = taffyString(R.string.taffy_privacy_what_happened),
                    summary = taffyString(R.string.taffy_privacy_what_happened_summary),
                    icon = TaffyIcon.Newspaper,
                    testTag = PRIVACY_WHAT_HAPPENED_TEST_TAG,
                    selected = false,
                    accentSelected = false,
                    onClick = { onIntent(PrivacyIntent.OpenWhatHappened) },
                )
            }
        }
        PrivacyDataActions(state = state, onIntent = onIntent)
    }
    PrivacyDeleteConfirmation(state = state, onIntent = onIntent)
}

@Composable
private fun PrivacyCopyCard(
    heading: String,
    body: String,
    icon: ImageVector,
    testTag: String,
    supportingContent: (@Composable () -> Unit)? = null,
) {
    TaffyObjectCard(testTag = testTag) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.Top,
        ) {
            SettingsGlyph(icon)
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            ) {
                Text(
                    text = heading,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                    modifier = Modifier.semantics { heading() },
                )
                Text(
                    text = body,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
                supportingContent?.invoke()
            }
        }
    }
}

/** Live totals whose values cannot carry a page, site, file, or account identity. */
@Composable
private fun PrivacyInventory(state: PrivacyUiState) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .testTag(PRIVACY_INVENTORY_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = taffyString(R.string.taffy_privacy_inventory_heading),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textPrimary,
        )
        PrivacyInventoryRow(R.string.taffy_privacy_inventory_workspaces, state.savedWorkspaces)
        PrivacyInventoryRow(R.string.taffy_privacy_inventory_library, state.libraryItems)
        PrivacyInventoryRow(R.string.taffy_privacy_inventory_memory, state.memoryItems)
        PrivacyInventoryRow(R.string.taffy_privacy_inventory_providers, state.connectedProviders)
        PrivacyInventoryRow(R.string.taffy_privacy_inventory_downloads, state.recentDownloads)
        PrivacyInventoryRow(R.string.taffy_privacy_inventory_sites, state.changedSites)
    }
}

@Composable
private fun PrivacyInventoryRow(labelResource: Int, count: PrivacyDataCount) {
    val value = when (count) {
        PrivacyDataCount.Loading -> taffyString(R.string.taffy_privacy_inventory_loading)
        PrivacyDataCount.Unavailable ->
            taffyString(R.string.taffy_privacy_inventory_unavailable)
        is PrivacyDataCount.Known -> if (count.isLowerBound) {
            taffyString(R.string.taffy_privacy_inventory_at_least, count.value)
        } else {
            taffyString(R.string.taffy_privacy_inventory_exact, count.value)
        }
    }
    Text(
        text = taffyString(
            R.string.taffy_privacy_inventory_row,
            taffyString(labelResource),
            value,
        ),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.textSecondary,
    )
}

internal fun privacyRouteBodyRes(route: ProviderRoute): Int = when (route) {
    ProviderRoute.NOT_CONFIGURED -> R.string.taffy_privacy_route_none
    ProviderRoute.DIRECT_WITH_YOUR_KEY -> R.string.taffy_privacy_route_direct
    ProviderRoute.NO_MODEL_REQUIRED -> R.string.taffy_privacy_route_no_model
}

const val PRIVACY_STORED_TEST_TAG: String = "privacy_stored"
const val PRIVACY_INVENTORY_TEST_TAG: String = "privacy_inventory"
const val PRIVACY_ROUTE_TEST_TAG: String = "privacy_route"
const val PRIVACY_RETENTION_TEST_TAG: String = "privacy_retention"
const val PRIVACY_LINKS_TEST_TAG: String = "privacy_links"
const val PRIVACY_SITE_SETTINGS_TEST_TAG: String = "privacy_site_settings"
const val PRIVACY_CLEAR_TEST_TAG: String = "privacy_clear"
const val PRIVACY_WHAT_HAPPENED_TEST_TAG: String = "privacy_what_happened"
