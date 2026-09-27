// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyEaseOut
import com.taffygo.browser.ui.core.designsystem.taffyRunOnce
import com.taffygo.browser.ui.core.designsystem.taffySegment
import com.taffygo.browser.ui.core.ui.TaffyBrandMark
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-007 — Get started. */
@Composable
fun GetStartedScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: GetStartedViewModel = screenViewModel(TaffyDestination.GetStarted)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    GetStartedContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun GetStartedContent(
    state: GetStartedUiState,
    onIntent: (GetStartedIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    /*
     * One run, in two phases: the mark and the words, then the card. Held
     * under reduced motion at its finished frame rather than at its first —
     * `taffyRunOnce` reports zero while it is not running, so a caller that
     * wants the ending has to say so, and a screen whose content faded in
     * from nothing would otherwise be a screen that never appears.
     */
    val reduced = TaffyTheme.reducedMotion
    val run by taffyRunOnce(running = !reduced, durationMillis = EntranceMillis)
    val words = if (reduced) 1f else taffyEaseOut(taffySegment(run, 0f, WordsShare))
    val card = if (reduced) 1f else taffyEaseOut(taffySegment(run, WordsShare, 1f))

    OnboardingSequence(
        destination = TaffyDestination.GetStarted,
        modifier = modifier,
        body = OnboardingBody.CENTER,
        footer = {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_onboarding_continue),
                onClick = { onIntent(GetStartedIntent.Continue) },
                modifier = Modifier.fillMaxWidth(),
                testTag = GET_STARTED_CONTINUE_TEST_TAG,
                size = TaffyButtonSize.LARGE,
            )
            Text(
                text = taffyString(R.string.taffy_get_started_data_link),
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
                textAlign = TextAlign.Center,
                textDecoration = TextDecoration.Underline,
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag(GET_STARTED_DATA_LINK_TEST_TAG)
                    .clickable { onIntent(GetStartedIntent.OpenDataSheet) },
            )
        },
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .alpha(words),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            TaffyBrandMark(size = MarkSize)
            Text(
                text = taffyString(R.string.taffy_get_started_headline),
                style = TaffyTheme.typography.display,
                color = TaffyTheme.colors.textPrimary,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag(GET_STARTED_HEADLINE_TEST_TAG),
            )
            Text(
                text = taffyString(R.string.taffy_get_started_body),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textSecondary,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth(),
            )
        }
        GetStartedProfileCard(
            state = state,
            onIntent = onIntent,
            modifier = Modifier.alpha(card),
        )
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .alpha(card)
                .testTag(GET_STARTED_PROMISES_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            GetStartedPromise(TaffyIcon.ShieldCheck, R.string.taffy_get_started_promise_account)
            GetStartedPromise(TaffyIcon.LockSimple, R.string.taffy_get_started_promise_servers)
            GetStartedPromise(TaffyIcon.Key, R.string.taffy_get_started_promise_key)
        }
    }
    if (state.dataSheetOpen) {
        GetStartedDataSheet(onDismiss = { onIntent(GetStartedIntent.CloseDataSheet) })
    }
}

/** One commitment: a glyph that carries no words, and the words beside it. */
@Composable
private fun GetStartedPromise(glyph: ImageVector, text: Int) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = glyph,
            contentDescription = null,
            tint = TaffyTheme.colors.positive,
            modifier = Modifier.size(PromiseIconSize),
        )
        Text(
            text = taffyString(text),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
    }
}

/** The tags screen SCR-007's semantics tests name. */
const val GET_STARTED_HEADLINE_TEST_TAG: String = "get_started_headline"
const val GET_STARTED_CARD_TEST_TAG: String = "get_started_card"
const val GET_STARTED_FACE_TEST_TAG: String = "get_started_face"
const val GET_STARTED_FACES_TEST_TAG: String = "get_started_faces"
const val GET_STARTED_FACE_TEST_TAG_PREFIX: String = "get_started_face_"
const val GET_STARTED_NAME_TEST_TAG: String = "get_started_name"
const val GET_STARTED_PROMISES_TEST_TAG: String = "get_started_promises"
const val GET_STARTED_CONTINUE_TEST_TAG: String = "get_started_continue"
const val GET_STARTED_DATA_LINK_TEST_TAG: String = "get_started_data_link"

private val MarkSize = 72.dp
private val PromiseIconSize = 18.dp

/** The entrance, and the share of it the words take before the card follows. */
private const val EntranceMillis = 420
private const val WordsShare = 0.45f
