// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyProjectContact
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-408 — About. */
@Composable
fun AboutScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: AboutViewModel = screenViewModel(TaffyDestination.About)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    AboutContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/**
 * Lockup, the two facts this build can vouch for, then the way to help, the
 * notices and the source.
 *
 * The screen states the app version and the Chromium version and stops there.
 * The revision hash, the downstream patch count and the management posture were
 * facts about the build rather than about the browsing (decision 0127). The
 * licences row opens the notices the package itself carries, and the source row
 * names the public repository, because someone holding this build may never
 * have seen the website (decision 0206).
 */
@Composable
fun AboutContent(
    state: AboutUiState,
    onIntent: (AboutIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val version = state.facts.versionName?.trim().orEmpty()
    TaffyScreen(
        destination = TaffyDestination.About,
        title = taffyString(R.string.taffy_about_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        AboutLockupCard()
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            SettingsHomeEyebrow(title = taffyString(R.string.taffy_about_facts_heading))
            AboutFactCard(
                heading = taffyString(R.string.taffy_about_version),
                body = if (version.isEmpty()) {
                    taffyString(R.string.taffy_about_version_unknown)
                } else {
                    version
                },
                testTag = ABOUT_VERSION_TEST_TAG,
            )
            state.facts.chromiumVersion?.takeIf(String::isNotBlank)?.let { chromiumVersion ->
                AboutFactCard(
                    heading = taffyString(R.string.taffy_about_chromium_version),
                    body = chromiumVersion,
                    testTag = ABOUT_CHROMIUM_VERSION_TEST_TAG,
                )
            }
        }
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            SettingsHomeEyebrow(title = taffyString(R.string.taffy_about_more_heading))
            TaffyGroupedCard {
                SettingsHomeRow(
                    title = taffyString(R.string.taffy_about_help),
                    summary = taffyString(R.string.taffy_about_help_summary),
                    icon = TaffyIcon.Question,
                    testTag = ABOUT_HELP_TEST_TAG,
                    selected = false,
                    accentSelected = true,
                    onClick = { onIntent(AboutIntent.OpenHelp) },
                )
                TaffyGroupedCardDivider()
                SettingsHomeRow(
                    title = taffyString(R.string.taffy_about_licences),
                    summary = taffyString(R.string.taffy_about_licences_summary),
                    icon = TaffyIcon.Article,
                    testTag = ABOUT_LICENCES_TEST_TAG,
                    selected = false,
                    accentSelected = true,
                    onClick = { onIntent(AboutIntent.OpenLicences) },
                )
                TaffyGroupedCardDivider()
                SettingsHomeRow(
                    title = taffyString(R.string.taffy_about_source),
                    summary = TaffyProjectContact.REPOSITORY.removePrefix("https://"),
                    icon = TaffyIcon.ArrowUpRight,
                    testTag = ABOUT_SOURCE_TEST_TAG,
                    selected = false,
                    accentSelected = true,
                    onClick = { onIntent(AboutIntent.OpenSource) },
                )
            }
        }
    }
}

const val ABOUT_LIST_TEST_TAG: String = "about_list"
const val ABOUT_VERSION_TEST_TAG: String = "about_version"
const val ABOUT_CHROMIUM_VERSION_TEST_TAG: String = "about_chromium_version"
const val ABOUT_HELP_TEST_TAG: String = "about_help"
const val ABOUT_LICENCES_TEST_TAG: String = "about_licences"
const val ABOUT_SOURCE_TEST_TAG: String = "about_source"
