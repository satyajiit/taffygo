// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeLeft
import androidx.compose.ui.unit.Density
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-002 — Meet Taffy.
 *
 * Four film demonstrations of Taffy working beside the current page. The carousel
 * is the screen; there is one forward action and no rename branch.
 */
class MeetTaffySemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<MeetTaffyIntent>()
    private val changed = mutableListOf<Boolean>()

    @Test
    fun theCarouselIsTheScreen() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                MeetTaffyContent(
                    onIntent = { intents += it },
                    soundEnabled = false,
                    onSoundEnabledChange = {},
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.MeetTaffy.screenId).assertExists()
        compose.onNodeWithTag(MEET_TAFFY_LOGO_TEST_TAG).assertExists()
        compose.onNodeWithTag(MEET_TAFFY_CAROUSEL_TEST_TAG).assertExists()
        // HorizontalPager may retain the adjacent page in the semantics tree
        // on a real device. The screen contract is that at least one carousel
        // visual is displayed, not that Compose owns exactly one composed
        // page at this instant.
        compose.onAllNodesWithTag(SHOWCASE_VISUAL_TEST_TAG).onFirst().assertIsDisplayed()
        compose.onNodeWithTag(SHOWCASE_SOUND_TEST_TAG).assertExists()
    }

    @Test
    fun theSoundControlSaysWhichStateItIsIn() {
        // PAR-A11Y-004: a muted film and a playing one may not differ by glyph
        // alone, so the button carries a state description either way.
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                MeetTaffyContent(
                    onIntent = { intents += it },
                    soundEnabled = true,
                    onSoundEnabledChange = { changed += it },
                )
            }
        }

        val on = context.getString(R.string.taffy_showcase_sound_state_on)
        compose.onNodeWithTag(SHOWCASE_SOUND_TEST_TAG)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, on))
            .performClick()

        assertEquals(listOf(false), changed)
    }

    @Test
    fun theOneForwardActionContinues() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                MeetTaffyContent(
                    onIntent = { intents += it },
                    soundEnabled = true,
                    onSoundEnabledChange = {},
                )
            }
        }

        compose.onNodeWithTag(MEET_TAFFY_CONTINUE_TEST_TAG).performClick()

        assertEquals(listOf(MeetTaffyIntent.Continue), intents)
    }

    @Test
    fun thePromptReservesTheSameHeightOnEverySlide() {
        showAtFontScale(1f)
        assertOneReservedHeight()
    }

    @Test
    fun thePromptReservesTheSameHeightOnEverySlideAtTwiceTheTextSize() {
        // The reservation is measured rather than approximated, so it has to
        // hold at a scale where the wrapping is completely different — which is
        // where a longest-by-character-count guess comes apart.
        showAtFontScale(2f)
        assertOneReservedHeight()
    }

    private fun showAtFontScale(fontScale: Float) {
        compose.setContent {
            val density = LocalDensity.current
            CompositionLocalProvider(
                LocalDensity provides Density(density.density, fontScale),
            ) {
                TaffyPreview(darkTheme = false, reducedMotion = true) {
                    MeetTaffyContent(
                        onIntent = { intents += it },
                        soundEnabled = false,
                        onSoundEnabledChange = {},
                    )
                }
            }
        }
    }

    /**
     * Walk the carousel and assert the prompt's text area never changes height.
     *
     * This is the shake, measured. The prompt types one character at a time
     * inside a column whose sibling is `weight(1f)`, so a text area that grew
     * by a line would resize the film above it — on every wrap while typing,
     * and again at every auto-advance because the questions differ in length.
     */
    private fun assertOneReservedHeight() {
        val first = reservedHeight()
        // One per slide, derived from the enum the slide list is built from
        // rather than a literal that a new slide would leave behind.
        repeat(ShowcaseVideo.entries.size - 1) { index ->
            compose.onNodeWithTag(MEET_TAFFY_CAROUSEL_TEST_TAG).performTouchInput { swipeLeft() }
            compose.waitForIdle()
            assertEquals(
                "slide ${index + 2} reserved a different height",
                first,
                reservedHeight(),
            )
        }
    }

    private fun reservedHeight(): Int =
        // The measurement node is deliberately hidden beneath its merged
        // prompt semantics; it is layout evidence, not a second accessible
        // question. Inspect the unmerged tree to measure it.
        compose.onNodeWithTag(SHOWCASE_QUESTION_SIZER_TEST_TAG, useUnmergedTree = true)
            .fetchSemanticsNode()
            .size
            .height
}
