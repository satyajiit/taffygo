// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBrandLockup
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-004 — choose how Taffy reaches a model. */
@Composable
fun AiSetupScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: AiSetupViewModel = screenViewModel(TaffyDestination.AiSetup)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    AiSetupContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        modifier = modifier,
    )
}

/** The progressive, stateless half: choose a route, then continue into it. */
@Composable
fun AiSetupContent(
    state: AiSetupUiState,
    onIntent: (AiSetupIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val footerAction = aiSetupFooterAction(state)
    OnboardingSequence(
        destination = TaffyDestination.AiSetup,
        modifier = modifier,
        footer = {
            TaffyPrimaryButton(
                label = taffyString(footerLabel(footerAction)),
                onClick = { onIntent(AiSetupIntent.Finish) },
                modifier = Modifier.fillMaxWidth(),
                enabled = state.hasChosen && !state.finishing,
                loading = state.finishing,
                testTag = AI_SETUP_PRIMARY_TEST_TAG,
                size = TaffyButtonSize.LARGE,
            )
            /*
             * Its own button, not the primary one relabelled. A screen that
             * turns "Choose provider" into "Set up later" when nothing is
             * selected makes one control mean two things, and a person who
             * looks away between reading and tapping gets the other one.
             */
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_ai_setup_set_up_later),
                onClick = { onIntent(AiSetupIntent.SetUpLater) },
                modifier = Modifier.fillMaxWidth(),
                enabled = !state.finishing,
                testTag = AI_SETUP_SKIP_TEST_TAG,
                size = TaffyButtonSize.LARGE,
            )
            Text(
                text = taffyString(R.string.taffy_ai_setup_change_later),
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag(AI_SETUP_FOOTER_NOTE_TEST_TAG),
            )
        },
    ) {
        AiSetupBand()
        AiSetupQuestion()
        AiSetupRouteSection(state = state, onIntent = onIntent)
        AiSetupAssurance()
    }
}

/**
 * The header band, in the start page's idiom rather than in one of its own.
 *
 * SCR-004 used to open with a centred portrait over a centred question.
 * Centring is right on SCR-007, which asks two questions with one answer
 * each and invites no comparison — but this screen is a comparison, and
 * that is the whole of the difference. Two cards are compared by reading down
 * one edge, and a centred hero over centred type does two things to that:
 * it gives the top of the screen to an illustration, and it leaves the
 * question on a different axis from the answers it is asking about.
 *
 * So the header is the construction
 * `docs/decisions/0048-the-start-page-carries-topics-and-a-band.md` settled for
 * the start page — a wash of the accent fading into the raised
 * surface, painted from the palette's own tokens rather than from a shipped
 * picture, correct in both themes without a second asset, carrying the mark in
 * a contrast chip because a lockup laid straight onto a gradient has contrast
 * that depends on where it lands. Everything below it starts at one leading
 * edge.
 */
@Composable
private fun AiSetupBand() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(BandHeight)
            .clip(TaffyTheme.shapes.card)
            .background(
                Brush.verticalGradient(
                    listOf(
                        TaffyTheme.colors.accentWash,
                        TaffyTheme.colors.surfaceRaised,
                    ),
                ),
            ),
    ) {
        Box(
            modifier = Modifier
                .padding(TaffyTheme.spacing.snug)
                .clip(TaffyTheme.shapes.pill)
                .background(TaffyTheme.colors.surface)
                .padding(
                    horizontal = TaffyTheme.spacing.snug,
                    vertical = TaffyTheme.spacing.step,
                ),
        ) {
            TaffyBrandLockup(height = LockupHeight, contentDescription = null)
        }
    }
}

/**
 * The question and the one sentence that qualifies it, as one block.
 *
 * Left-aligned rather than centred, and a single step apart rather than a row
 * gap, because they are one thought: the sentence answers "and what happens if
 * I pick wrong", which is only readable as an answer if it sits under the
 * question rather than beside it in the same visual weight.
 */
@Composable
private fun AiSetupQuestion() {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
    ) {
        Text(
            text = taffyString(R.string.taffy_ai_setup_title),
            style = TaffyTheme.typography.display,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier
                .fillMaxWidth()
                .testTag(AI_SETUP_TITLE_TEST_TAG)
                .semantics { heading() },
        )
        Text(
            text = taffyString(R.string.taffy_ai_setup_body),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

/**
 * The routes, headed the way every list in this product is headed: what the
 * section is on the left, how much of it there is on the right.
 *
 * The count is read off the list rather than written down, so a third route
 * added to [AiSetupRoutes] changes the heading by existing. It is a plural
 * resource because "1 way" and "2 ways" are different sentences in English and
 * because a language with more forms than English must be able to say so.
 *
 * A single step between the cards, not a row gap: they are two answers to one
 * question and belong to each other more than they belong to the sections
 * above and below, which sit a full gap away.
 */
@Composable
private fun AiSetupRouteSection(
    state: AiSetupUiState,
    onIntent: (AiSetupIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = taffyString(R.string.taffy_ai_setup_routes_label),
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                text = taffyPlural(
                    R.plurals.taffy_ai_setup_routes_count,
                    AiSetupRoutes.size,
                    AiSetupRoutes.size,
                ),
                style = TaffyTheme.typography.micro,
                color = TaffyTheme.colors.textTertiary,
            )
        }
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .selectableGroup()
                .testTag(AI_SETUP_ROUTES_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            AiSetupRoutes.forEach { option ->
                AiSetupRouteCard(
                    option = option,
                    chosen = state.route == option.route,
                    enabled = !state.finishing,
                    onChoose = { onIntent(AiSetupIntent.ChooseRoute(option.route)) },
                )
            }
        }
    }
}

/** What the one action calls itself, which is the whole of what it promises. */
private fun footerLabel(action: AiSetupFooterAction): Int =
    when (action) {
        AiSetupFooterAction.CONTINUE -> R.string.taffy_ai_setup_continue
        AiSetupFooterAction.CHOOSE_PROVIDER -> R.string.taffy_ai_setup_choose_provider
    }

/** The tags screen SCR-004's semantics tests name. */
const val AI_SETUP_TITLE_TEST_TAG: String = "ai_setup_title"
const val AI_SETUP_ROUTES_TEST_TAG: String = "ai_setup_routes"
const val AI_SETUP_ROUTE_TEST_TAG_PREFIX: String = "ai_setup_route_"
const val AI_SETUP_FOOTNOTE_TEST_TAG: String = "ai_setup_footnote"
const val AI_SETUP_PRIMARY_TEST_TAG: String = "ai_setup_primary"
const val AI_SETUP_SKIP_TEST_TAG: String = "ai_setup_skip"
const val AI_SETUP_FOOTER_NOTE_TEST_TAG: String = "ai_setup_footer_note"

// The start page's band is 118: a mark in a contrast chip and nothing else.
// This header is the same construction, so it is the same height.
private val BandHeight = 118.dp
private val LockupHeight = 22.dp
