// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-409 — Help and feedback. */
@Composable
fun HelpScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: HelpViewModel = screenViewModel(TaffyDestination.HelpAndFeedback)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    HelpContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/**
 * Stacked limit cards, then the two ways to reach the people who make TaffyGo.
 *
 * There used to be a "Share diagnostics" switch here and on the privacy
 * centre. Nothing read it and TaffyGo has nowhere to send such facts
 * (decision 0200), so it promised a report that could never be made and left.
 */
@Composable
fun HelpContent(
    state: HelpUiState,
    onIntent: (HelpIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.HelpAndFeedback,
        title = taffyString(R.string.taffy_help_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .testTag(HELP_LIMITS_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            SettingsHomeEyebrow(title = taffyString(R.string.taffy_help_limits_heading))
            TaffyInfoTile {
                Text(
                    text = taffyString(R.string.taffy_help_limits_body),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                HelpLimitCard(
                    title = taffyString(R.string.taffy_help_limit_assistant),
                    body = taffyString(R.string.taffy_help_limit_assistant_detail),
                    icon = TaffyIcon.Sparkle,
                    testTag = HELP_LIMIT_ASSISTANT_TEST_TAG,
                )
                HelpLimitCard(
                    title = taffyString(R.string.taffy_help_limit_blocking),
                    body = taffyString(R.string.taffy_help_limit_blocking_detail),
                    icon = TaffyIcon.ShieldCheck,
                    testTag = HELP_LIMIT_BLOCKING_TEST_TAG,
                )
                HelpLimitCard(
                    title = taffyString(R.string.taffy_help_limit_secrets),
                    body = taffyString(R.string.taffy_help_limit_secrets_detail),
                    icon = TaffyIcon.LockSimple,
                    testTag = HELP_LIMIT_SECRETS_TEST_TAG,
                )
            }
        }
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            SettingsHomeEyebrow(title = taffyString(R.string.taffy_help_reach_heading))
            HelpReachCard(
                emailUnavailable = state.emailUnavailable,
                onIntent = onIntent,
            )
        }
    }
}

const val HELP_REACH_TEST_TAG: String = "help_reach"
const val HELP_FEEDBACK_EMAIL_TEST_TAG: String = "help_feedback_email"
const val HELP_FEEDBACK_ISSUE_TEST_TAG: String = "help_feedback_issue"
const val HELP_FEEDBACK_NO_EMAIL_TEST_TAG: String = "help_feedback_no_email"
const val HELP_LIMITS_TEST_TAG: String = "help_limits"
const val HELP_LIMIT_ASSISTANT_TEST_TAG: String = "help_limit_assistant"
const val HELP_LIMIT_BLOCKING_TEST_TAG: String = "help_limit_blocking"
const val HELP_LIMIT_SECRETS_TEST_TAG: String = "help_limit_secrets"
