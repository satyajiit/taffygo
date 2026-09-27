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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyIconButton
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-708 — the browser profile this device keeps.
 *
 * 1.0 ships one profile (decision 0255): there is no Add profile and no switch,
 * because Chromium on Android can build only its first profile. The row names
 * it and counts its workspaces. A profile an earlier build left behind is still
 * listed, and can be deleted, but never opened.
 */
@Composable
fun BrowserProfilesScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: BrowserProfilesViewModel = screenViewModel(TaffyDestination.BrowserProfiles)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    BrowserProfilesContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** Stateless SCR-708 content. */
@Composable
fun BrowserProfilesContent(
    state: BrowserProfilesUiState,
    onIntent: (BrowserProfilesIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyLazyScreen(
        destination = TaffyDestination.BrowserProfiles,
        title = taffyString(R.string.taffy_profiles_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = Modifier.testTag(BROWSER_PROFILES_LIST_TEST_TAG),
    ) {
        item(key = "profiles-explanation", contentType = "explanation") {
            Text(
                text = taffyString(R.string.taffy_profiles_explanation),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        when (state.availability) {
            BrowserProfilesRepository.Availability.LOADING -> profilesLoading()
            BrowserProfilesRepository.Availability.UNAVAILABLE -> profilesUnavailable(onIntent)
            BrowserProfilesRepository.Availability.READY -> profilesReady(state, onIntent)
        }
        profileStatus(state, onIntent)
    }
    DeleteProfileSheet(state, onIntent)
}

private fun LazyListScope.profilesLoading() {
    item(key = "profiles-loading", contentType = "status") {
        TaffyGroupedCard {
            repeat(3) {
                TaffySkeleton(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(ProfileSkeletonHeight)
                        .padding(TaffyTheme.spacing.screenMargin),
                    accessibleDescription = if (it == 0) {
                        taffyString(R.string.taffy_profiles_loading)
                    } else {
                        null
                    },
                )
            }
        }
    }
}

private fun LazyListScope.profilesUnavailable(onIntent: (BrowserProfilesIntent) -> Unit) {
    item(key = "profiles-unavailable", contentType = "status") {
        TaffyEmptyState(
            title = taffyString(R.string.taffy_profiles_unavailable_title),
            body = taffyString(R.string.taffy_profiles_unavailable_body),
            leading = { ProfilesGlyph() },
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_profiles_try_again),
            onClick = { onIntent(BrowserProfilesIntent.Refresh) },
            testTag = BROWSER_PROFILES_RETRY_TEST_TAG,
        )
    }
}

private fun LazyListScope.profilesReady(
    state: BrowserProfilesUiState,
    onIntent: (BrowserProfilesIntent) -> Unit,
) {
    items(
        items = state.profiles,
        key = BrowserProfilesRepository.Profile::id,
        contentType = { "profile" },
    ) { profile ->
        BrowserProfileRow(profile, state, onIntent)
    }
}

@Composable
private fun BrowserProfileRow(
    profile: BrowserProfilesRepository.Profile,
    state: BrowserProfilesUiState,
    onIntent: (BrowserProfilesIntent) -> Unit,
) {
    val workspaceCount = state.activeWorkspaceCount
    val rowDescription = when {
        profile.active && workspaceCount != null -> taffyPlural(
            R.plurals.taffy_profiles_row_active_workspaces,
            workspaceCount,
            profile.displayName,
            workspaceCount,
        )
        profile.active -> taffyString(R.string.taffy_profiles_row_active, profile.displayName)
        else -> taffyString(R.string.taffy_profiles_row_unused, profile.displayName)
    }
    TaffyGroupedCard {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = ProfileRowMinimumHeight)
                .padding(TaffyTheme.spacing.screenMargin)
                .testTag("$BROWSER_PROFILE_ROW_TEST_TAG_PREFIX${profile.id}")
                .semantics(mergeDescendants = true) { contentDescription = rowDescription },
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SettingsGlyph(if (profile.active) TaffyIcon.CheckCircle else TaffyIcon.UserCircle)
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(profile.displayName, style = TaffyTheme.typography.body)
                Text(
                    text = when {
                        profile.active && workspaceCount != null -> taffyPlural(
                            R.plurals.taffy_profiles_active_workspaces,
                            workspaceCount,
                            workspaceCount,
                        )
                        profile.active -> taffyString(R.string.taffy_profiles_active)
                        else -> taffyString(R.string.taffy_profiles_unused)
                    },
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            if (!profile.active && state.profiles.size > 1) {
                TaffyIconButton(
                    icon = TaffyIcon.Trash,
                    contentDescription = taffyString(
                        R.string.taffy_profiles_delete_description,
                        profile.displayName,
                    ),
                    onClick = { onIntent(BrowserProfilesIntent.AskToDelete(profile.id)) },
                    enabled = !state.busy,
                    testTag = "$BROWSER_PROFILE_DELETE_TEST_TAG_PREFIX${profile.id}",
                )
            }
        }
    }
}

@Composable
private fun ProfilesGlyph() {
    Icon(
        imageVector = TaffyIcon.UsersThree,
        contentDescription = null,
        tint = TaffyTheme.colors.textPrimary,
        modifier = Modifier.size(ProfileGlyphSize),
    )
}

const val BROWSER_PROFILES_LIST_TEST_TAG: String = "browser_profiles_list"
const val BROWSER_PROFILES_RETRY_TEST_TAG: String = "browser_profiles_retry"
const val BROWSER_PROFILES_STATUS_TEST_TAG: String = "browser_profiles_status"
const val BROWSER_PROFILES_ERROR_TEST_TAG: String = "browser_profiles_error"
const val BROWSER_PROFILES_DELETE_SHEET_TEST_TAG: String = "browser_profiles_delete_sheet"
const val BROWSER_PROFILES_CONFIRM_DELETE_TEST_TAG: String = "browser_profiles_confirm_delete"
const val BROWSER_PROFILE_ROW_TEST_TAG_PREFIX: String = "browser_profile_row_"
const val BROWSER_PROFILE_DELETE_TEST_TAG_PREFIX: String = "browser_profile_delete_"

private val ProfileRowMinimumHeight = 64.dp
private val ProfileSkeletonHeight = 52.dp
private val ProfileGlyphSize = 32.dp
