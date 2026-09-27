// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.sp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFailure
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Who the page is about, and whether a credential already stands behind them. */
@Composable
internal fun ProviderSignInHeader(state: ProviderSignInUiState) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ProviderBadge(providerId = state.providerId, name = state.displayName)
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = state.displayName,
                style = TaffyTheme.typography.headline,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = if (state.connected) {
                    taffyString(R.string.taffy_providers_signin_connected_note, state.displayName)
                } else {
                    taffyString(R.string.taffy_providers_signin_blurb)
                },
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

/**
 * One panel per stage, and every one of them says what to do next.
 *
 * Cancellation is offered from every running stage, including the exchange.
 * The screen changes only after the exact portable flow removal is accepted.
 */
@Composable
internal fun ProviderSignInStagePanel(
    state: ProviderSignInUiState,
    onIntent: (ProviderSignInIntent) -> Unit,
) {
    when (val stage = state.stage) {
        ProviderSignInStage.Idle -> StartPanel(state = state, onIntent = onIntent)

        ProviderSignInStage.Starting -> ProgressPanel(
            title = taffyString(R.string.taffy_providers_signin_starting, state.displayName),
            body = taffyString(R.string.taffy_providers_signin_waiting_body),
            testTag = PROVIDER_SIGN_IN_STARTING_TEST_TAG,
            onIntent = onIntent,
        )

        is ProviderSignInStage.CodeReady -> DeviceCodePanel(stage = stage, onIntent = onIntent)

        // A redirect wait carries its way back (decision 0095 section 2); a
        // device-code wait has none and is a plain wait.
        is ProviderSignInStage.Waiting -> when (val entry = stage.codeEntry) {
            null -> ProgressPanel(
                title = taffyString(R.string.taffy_providers_signin_waiting_title),
                body = taffyString(R.string.taffy_providers_signin_waiting_body),
                testTag = PROVIDER_SIGN_IN_WAITING_TEST_TAG,
                onIntent = onIntent,
            )

            else -> WaitingWithCodeEntryPanel(entry = entry, onIntent = onIntent)
        }

        ProviderSignInStage.Exchanging -> ProgressPanel(
            title = taffyString(R.string.taffy_providers_signin_exchanging),
            body = taffyString(R.string.taffy_providers_signin_exchanging_body),
            testTag = PROVIDER_SIGN_IN_EXCHANGING_TEST_TAG,
            onIntent = onIntent,
        )

        ProviderSignInStage.Succeeded -> OutcomePanel(
            title = taffyString(R.string.taffy_providers_signin_done_title),
            body = taffyString(R.string.taffy_providers_signin_done_body, state.displayName),
            actionLabel = taffyString(R.string.taffy_providers_signin_done_cta),
            onAction = { onIntent(ProviderSignInIntent.OpenProviderPage) },
            testTag = PROVIDER_SIGN_IN_DONE_TEST_TAG,
        )

        is ProviderSignInStage.Failed -> OutcomePanel(
            title = taffyString(R.string.taffy_providers_signin_failed_title),
            body = taffyString(failureRes(stage.failure)),
            actionLabel = taffyString(R.string.taffy_providers_signin_try_again),
            onAction = { onIntent(ProviderSignInIntent.DismissFailure) },
            testTag = PROVIDER_SIGN_IN_FAILED_TEST_TAG,
        )

        ProviderSignInStage.Cancelled -> OutcomePanel(
            title = taffyString(R.string.taffy_providers_signin_cancelled_title),
            body = taffyString(R.string.taffy_providers_signin_cancelled_body),
            actionLabel = taffyString(R.string.taffy_providers_action_sign_in),
            onAction = { onIntent(ProviderSignInIntent.Start) },
            testTag = PROVIDER_SIGN_IN_CANCELLED_TEST_TAG,
        )
    }
}

/** Nothing is running: one sentence and the one action. */
@Composable
private fun StartPanel(
    state: ProviderSignInUiState,
    onIntent: (ProviderSignInIntent) -> Unit,
) {
    TaffyPrimaryButton(
        label = taffyString(
            if (state.connected) {
                R.string.taffy_providers_managed_reauthenticate
            } else {
                R.string.taffy_providers_action_sign_in
            },
        ),
        onClick = { onIntent(ProviderSignInIntent.Start) },
        enabled = state.startable,
        icon = TaffyIcon.ShieldCheck,
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_SIGN_IN_START_TEST_TAG),
    )
}

/**
 * The device flow's code, drawn where the person is looking.
 *
 * Three things the row this replaces did not have: the verification page opens
 * with a tap, the code copies with a tap, and the wait has a visible end. The
 * countdown is this screen's own window — the vendor's expiry never reaches
 * Android — and the note under it says so rather than implying the number came
 * from the vendor.
 */
@Composable
private fun DeviceCodePanel(
    stage: ProviderSignInStage.CodeReady,
    onIntent: (ProviderSignInIntent) -> Unit,
) {
    SignInPanel(testTag = PROVIDER_SIGN_IN_CODE_TEST_TAG) {
        Text(
            text = taffyString(R.string.taffy_providers_signin_code_hint),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
        // Monospace and spaced out: this is the one string in the product a
        // person retypes character by character on another device, where an
        // l against a 1 costs the whole flow. The design system carries no
        // mono face, so the platform's is asked for here and nowhere else.
        Text(
            text = stage.userCode,
            style = TaffyTheme.typography.display.copy(
                fontFamily = FontFamily.Monospace,
                letterSpacing = CodeLetterSpacing,
            ),
            color = TaffyTheme.colors.textPrimary,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .fillMaxWidth()
                .testTag(PROVIDER_SIGN_IN_CODE_VALUE_TEST_TAG)
                .semantics {
                    // Read one character at a time; a screen reader saying
                    // "bdwn" as a word is a code nobody can transcribe.
                    contentDescription = stage.userCode.toList().joinToString(" ")
                },
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_providers_signin_code_open),
                onClick = { onIntent(ProviderSignInIntent.OpenVerificationPage) },
                icon = TaffyIcon.ArrowUpRight,
                size = TaffyButtonSize.COMPACT,
                modifier = Modifier.weight(1f),
                testTag = PROVIDER_SIGN_IN_CODE_OPEN_TEST_TAG,
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_providers_signin_code_copy),
                onClick = { onIntent(ProviderSignInIntent.CopyUserCode) },
                icon = TaffyIcon.CopySimple,
                size = TaffyButtonSize.COMPACT,
                modifier = Modifier.weight(1f),
                testTag = PROVIDER_SIGN_IN_CODE_COPY_TEST_TAG,
            )
        }
        Text(
            text = taffyString(
                R.string.taffy_providers_signin_window,
                clockOf(stage.remainingSeconds),
            ),
            style = TaffyTheme.typography.numeric,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier
                .testTag(PROVIDER_SIGN_IN_COUNTDOWN_TEST_TAG)
                .semantics { liveRegion = LiveRegionMode.Polite },
        )
        Text(
            text = taffyString(R.string.taffy_providers_signin_window_note),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
        )
        SignInCancelAction(onIntent = onIntent)
    }
}

/** A stage that is waiting on somebody else, with the way out under it. */
@Composable
private fun ProgressPanel(
    title: String,
    body: String,
    testTag: String,
    onIntent: (ProviderSignInIntent) -> Unit,
) {
    SignInPanel(testTag = testTag) {
        Text(
            text = title,
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
        )
        Text(
            text = body,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        SignInCancelAction(onIntent = onIntent)
    }
}

/** A stage that has ended, with the one thing to do about it. */
@Composable
private fun OutcomePanel(
    title: String,
    body: String,
    actionLabel: String,
    onAction: () -> Unit,
    testTag: String,
) {
    SignInPanel(testTag = testTag) {
        Text(
            text = title,
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
        )
        Text(
            text = body,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyPrimaryButton(
            label = actionLabel,
            onClick = onAction,
            modifier = Modifier.fillMaxWidth(),
            testTag = "$PROVIDER_SIGN_IN_OUTCOME_ACTION_PREFIX$testTag",
        )
    }
}

/** The way out of every running stage; shared with the manual-code panel. */
@Composable
internal fun SignInCancelAction(onIntent: (ProviderSignInIntent) -> Unit) {
    TaffySecondaryButton(
        label = taffyString(R.string.taffy_providers_signin_cancel),
        onClick = { onIntent(ProviderSignInIntent.Cancel) },
        icon = TaffyIcon.X,
        modifier = Modifier.fillMaxWidth(),
        testTag = PROVIDER_SIGN_IN_CANCEL_TEST_TAG,
    )
}

/** The card every stage panel on this screen is drawn in. */
@Composable
internal fun SignInPanel(testTag: String, content: @Composable () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.cardPadding)
            .testTag(testTag),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        content()
    }
}

/** Minutes and seconds, in the locale's own digits. */
@Composable
private fun clockOf(remainingSeconds: Int): String = taffyString(
    R.string.taffy_providers_signin_clock,
    remainingSeconds / SECONDS_PER_MINUTE,
    remainingSeconds % SECONDS_PER_MINUTE,
)

private fun failureRes(failure: ProviderSignInFailure): Int = when (failure) {
    ProviderSignInFailure.DENIED -> R.string.taffy_providers_signin_failed_denied
    ProviderSignInFailure.PROVIDER_ERROR -> R.string.taffy_providers_signin_failed_provider
    ProviderSignInFailure.TIMED_OUT -> R.string.taffy_providers_signin_failed_timeout
    ProviderSignInFailure.UNAVAILABLE -> R.string.taffy_providers_signin_failed_unavailable
    ProviderSignInFailure.NOT_ADMITTED -> R.string.taffy_providers_signin_failed_refused
}

/** The tags screen SCR-416's semantics tests name. */
const val PROVIDER_SIGN_IN_START_TEST_TAG: String = "providers_sign_in_start"
const val PROVIDER_SIGN_IN_STARTING_TEST_TAG: String = "providers_sign_in_starting"
const val PROVIDER_SIGN_IN_WAITING_TEST_TAG: String = "providers_sign_in_waiting"
const val PROVIDER_SIGN_IN_EXCHANGING_TEST_TAG: String = "providers_sign_in_exchanging"
const val PROVIDER_SIGN_IN_CODE_TEST_TAG: String = "providers_sign_in_code"
const val PROVIDER_SIGN_IN_CODE_VALUE_TEST_TAG: String = "providers_sign_in_code_value"
const val PROVIDER_SIGN_IN_CODE_OPEN_TEST_TAG: String = "providers_sign_in_code_open"
const val PROVIDER_SIGN_IN_CODE_COPY_TEST_TAG: String = "providers_sign_in_code_copy"
const val PROVIDER_SIGN_IN_COUNTDOWN_TEST_TAG: String = "providers_sign_in_countdown"
const val PROVIDER_SIGN_IN_CANCEL_TEST_TAG: String = "providers_sign_in_cancel"
const val PROVIDER_SIGN_IN_DONE_TEST_TAG: String = "providers_sign_in_done"
const val PROVIDER_SIGN_IN_FAILED_TEST_TAG: String = "providers_sign_in_failed"
const val PROVIDER_SIGN_IN_CANCELLED_TEST_TAG: String = "providers_sign_in_cancelled"
const val PROVIDER_SIGN_IN_OUTCOME_ACTION_PREFIX: String = "action_"

private val CodeLetterSpacing = 4.sp
private const val SECONDS_PER_MINUTE = 60
