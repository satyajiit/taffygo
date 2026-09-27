// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.controlTestTag
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-301 — one line of truth about one assistant.
 *
 * The line is announced when it changes, because a bar that changes silently is
 * a bar a screen-reader user has to keep checking. Partial work keeps its own
 * words: this test asserts the partly-done line says what was read and what was
 * not, rather than rounding the work up.
 *
 * **Every case here renders the bar inside the row it is actually drawn into.**
 * See [bar]. They did not, and that is precisely why a 160 dp card sat inside a
 * 56 dp action row on a phone while this file was green from top to bottom.
 */
class AssistantBarSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<AssistantBarIntent>()

    // The bar under test, held in state rather than closed over: `setContent`
    // may be called once per test, and the two cases that sweep every state
    // need to show ten of them.
    private val shown = mutableStateOf(AssistantBarUiState())
    private val darkTheme = mutableStateOf(false)
    private var mounted = false

    /**
     * The bar, in the chassis the browser gives it.
     *
     * `BrowserActionRow` is a fixed `ActionRowHeight` and clips what it holds,
     * so a bar rendered without that bound is a bar no assertion in this file
     * can catch growing out of it. The number is restated rather than imported
     * because `:feature:browsing` is this module's sibling, not its dependency;
     * if it moves there it must move here, and the pill's own height assertion
     * below is what would notice.
     *
     * Mounted once and then fed by state, because the rule admits exactly one
     * `setContent` per test and the sweeps below show ten bars each.
     */
    private fun bar(state: AssistantBarUiState, dark: Boolean = false) {
        shown.value = state
        darkTheme.value = dark
        if (!mounted) {
            mounted = true
            compose.setContent {
                TaffyPreview(darkTheme = darkTheme.value, reducedMotion = true) {
                    Chassis {
                        AssistantBarContent(state = shown.value, onIntent = { intents += it })
                    }
                }
            }
        }
        compose.waitForIdle()
    }

    @Composable
    private fun Chassis(content: @Composable () -> Unit) {
        Box(Modifier.height(ActionRowChassis).clipToBounds()) { content() }
    }

    @Test
    fun theBarIsALiveRegionSoItsLineIsAnnounced() {
        bar(AssistantPreviewStates.running)

        compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG).assert(
            SemanticsMatcher.keyIsDefined(SemanticsProperties.LiveRegion),
        )
    }

    /**
     * The invariant the action row depends on, asserted for every state there
     * is rather than for the one somebody thought of.
     *
     * A state whose content is taller than the pill does not fail anywhere: the
     * row clips it, the build is green, and the phone shows a sliced card. So
     * the height is the assertion, and `entries` is what makes a tenth state
     * impossible to add without meeting it.
     */
    @Test
    fun everyStateIsOnePillOfTheHeightTheActionRowGivesIt() {
        barStates().forEach { (name, state) ->
            intents.clear()
            bar(state)
            compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG)
                .assertIsDisplayed()
                .assertHeightIsEqualTo(PillHeight)
            compose.onNodeWithTag(ASSISTANT_LINE_TEST_TAG, useUnmergedTree = true)
                .assertExists()
            assertEquals("$name drew no pill", 0, intents.size)
        }
    }

    /** Every final state announces its own word, which the status chip used to carry. */
    @Test
    fun everyFinalStateAnnouncesItsWord() {
        TaskDisplayState.entries.filter { it.isFinal }.forEach { display ->
            bar(AssistantPreviewStates.done.copy(state = display))
            val word = context.getString(StatusPresentation.of(display).labelRes)
            compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG).assert(
                SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, word),
            )
        }
    }

    @Test
    fun theRunningLineNamesTheAssistantAndTheCount() {
        bar(AssistantPreviewStates.running)

        val expected = context.resources.getQuantityString(
            R.plurals.taffy_assistant_running,
            4,
            4,
        )
        // The line is read as part of the element that holds it — the pill is one
        // target — so the line's own node lives in the unmerged tree. That
        // merging is the accessible behaviour the UX spec asks for; this asserts
        // the words it contributes.
        compose.onNodeWithTag(ASSISTANT_LINE_TEST_TAG, useUnmergedTree = true)
            .assertContentDescriptionEquals(expected)
    }

    /**
     * The bar said "Taffy is comparing 1 page…" beside a task in a build where
     * nothing reads a page. The count was right and the sentence was false, so
     * the line is replaced rather than qualified — one line has no room for a
     * qualification anybody would read.
     */
    @Test
    fun aTaskNothingIsDrivingGetsAnHonestLineInsteadOfACount() {
        bar(AssistantPreviewStates.runningNotDriven)

        compose.onNodeWithTag(ASSISTANT_LINE_TEST_TAG, useUnmergedTree = true)
            .assertContentDescriptionEquals(context.getString(R.string.taffy_assistant_core_unavailable))
        // And nothing travels round it and it offers no control, because a pill
        // that moves under that line is the claim the line just withdrew.
        compose.onNodeWithTag(controlTestTag(TaskControl.TAKE_OVER)).assertExists()
        compose.onNodeWithTag(controlTestTag(TaskControl.PAUSE)).assertDoesNotExist()
    }

    @Test
    fun partialWorkKeepsItsOwnWordsAndIsNotRoundedUp() {
        bar(AssistantPreviewStates.partlyDone, dark = true)

        val expected = context.getString(
            R.string.taffy_assistant_partly_done,
            context.resources.getQuantityString(R.plurals.taffy_count_pages_read, 3, 3),
        )
        compose.onNodeWithTag(ASSISTANT_LINE_TEST_TAG, useUnmergedTree = true)
            .assertContentDescriptionEquals(expected)
    }

    /**
     * The running pill carries take over, and nothing else.
     *
     * Take over is here rather than one row above because decision 0141 took
     * that row away: the takeover band said what this line says and carried the
     * one control that gives the page back, so a person reaching for it had to
     * find it on the surface that was not the bar. Stop stays on the task view
     * the pill opens (UX spec sections 3 and 6).
     *
     * **Pause is asserted absent.** It was the running pill's own chip until
     * decision 0142: it is not a thing a person wants of a task that is
     * working, and the width it took is the sentence's. The control itself is
     * unchanged — the reducer still admits it and the task view and the
     * notification still offer it — so this asserts the bar, not the control.
     */
    @Test
    fun theRunningPillCarriesTakeOverAndNothingElse() {
        bar(AssistantPreviewStates.running)

        compose.onNodeWithTag(controlTestTag(TaskControl.TAKE_OVER))
            .assertExists()
            .assertIsDisplayed()
            .performClick()
        assertEquals(listOf(AssistantBarIntent.Control(TaskControl.TAKE_OVER)), intents)

        listOf(TaskControl.PAUSE, TaskControl.STOP, TaskControl.RESUME).forEach { control ->
            compose.onNodeWithTag(controlTestTag(control)).assertDoesNotExist()
        }
    }

    /**
     * Take over is the reducer's answer, never a guess from the phase.
     *
     * A finished task admits no controls at all, and the chip a surface draws
     * anyway is a promise the reducer has not made (parity row PAR-A11Y-002).
     */
    @Test
    fun aFinishedPillOffersNoTakeOver() {
        bar(AssistantPreviewStates.done)

        compose.onNodeWithTag(controlTestTag(TaskControl.TAKE_OVER)).assertDoesNotExist()
    }

    /**
     * The held pill's own control, and only where the reducer admits it.
     *
     * Resume is the one thing the bar performs rather than navigates to, so the
     * chip carries the same name every other surface's resume carries — and a
     * revision that does not admit it draws the same pill with no chip, because
     * a control the reducer would refuse must not be offered.
     */
    @Test
    fun resumeIsTheHeldPillsOwnControlOnlyWhereItIsAdmitted() {
        bar(AssistantPreviewStates.paused)

        compose.onNodeWithTag(controlTestTag(TaskControl.RESUME))
            .assertExists()
            .assertIsDisplayed()
            .performClick()
        assertEquals(listOf(AssistantBarIntent.Control(TaskControl.RESUME)), intents)

        intents.clear()
        bar(AssistantPreviewStates.pausedWithoutResume)
        compose.onNodeWithTag(controlTestTag(TaskControl.RESUME)).assertDoesNotExist()
    }

    @Test
    fun anIdleBarOffersNoControlsAndStillSaysWhereItIs() {
        bar(AssistantBarUiState())

        compose.onNodeWithTag(ASSISTANT_LINE_TEST_TAG, useUnmergedTree = true)
            .assertContentDescriptionEquals(
                context.getString(R.string.taffy_assistant_idle, "Taffy"),
            )
        compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG)
            .assertExists()
            .assertIsDisplayed()
            .assertHasClickAction()
            .performClick()
        TaskControl.entries.forEach { control ->
            compose.onNodeWithTag(controlTestTag(control)).assertDoesNotExist()
        }
        assertEquals(listOf(AssistantBarIntent.OpenAssistant), intents)
    }

    @Test
    fun aBarThatIsNotSetUpInvitesSetup() {
        bar(AssistantPreviewStates.notSetUp)

        compose.onNodeWithTag(ASSISTANT_LINE_TEST_TAG, useUnmergedTree = true)
            .assertContentDescriptionEquals(context.getString(R.string.taffy_assistant_not_set_up))
        compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG)
            .assertHasClickAction()
            .performClick()

        assertEquals(listOf(AssistantBarIntent.OpenAssistant), intents)
    }

    @Test
    fun aFailedPillNamesTheProvider() {
        bar(AssistantPreviewStates.failedProvider)

        compose.onNodeWithTag(ASSISTANT_LINE_TEST_TAG, useUnmergedTree = true)
            .assertContentDescriptionEquals(context.getString(R.string.taffy_assistant_failed_provider))
    }

    /**
     * A finished task's pill is itself the way to the results.
     *
     * The chip that used to carry those words is gone, and not for tidiness: a
     * chip wide enough to say "See results" leaves the line about fifty
     * density-independent pixels on a phone, and the line is the one thing the
     * spec fixes word for word.
     */
    @Test
    fun aFinishedTaskOffersTheWayToItsResults() {
        bar(AssistantPreviewStates.done)

        compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG)
            .assertExists()
            .assertIsDisplayed()
            .assertHasClickAction()
            .performClick()

        assertEquals(listOf(AssistantBarIntent.OpenResults), intents)
    }

    @Test
    fun partialWorkKeepsTheWayToItsResultsToo() {
        bar(AssistantPreviewStates.partlyDone)

        compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG).assertHasClickAction().performClick()

        assertEquals(listOf(AssistantBarIntent.OpenResults), intents)
    }

    @Test
    fun aRunningTaskOffersNoResultsYet() {
        bar(AssistantPreviewStates.running)

        compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG).performClick()

        assertEquals(listOf(AssistantBarIntent.OpenAssistant), intents)
    }

    @Test
    fun theWholeBarOpensTheTaskView() {
        bar(AssistantPreviewStates.waitingForYou)

        compose.onNodeWithTag(ASSISTANT_BAR_TEST_TAG).performClick()

        assertEquals(listOf(AssistantBarIntent.OpenAssistant), intents)
    }

    /** One bar state per pill state, so the height case covers all nine. */
    private fun barStates(): List<Pair<String, AssistantBarUiState>> = listOf(
        "idle" to AssistantBarUiState(),
        "running" to AssistantPreviewStates.running,
        "waiting" to AssistantPreviewStates.waitingForYou,
        "paused" to AssistantPreviewStates.paused,
        "held" to AssistantPreviewStates.runningNotDriven,
        "held without resume" to AssistantPreviewStates.pausedWithoutResume,
        "done" to AssistantPreviewStates.done,
        "partly done" to AssistantPreviewStates.partlyDone,
        "stopped" to AssistantPreviewStates.stopped,
        "failed" to AssistantPreviewStates.failedProvider,
    )

    private companion object {
        /** `BrowserActionRow`'s `ActionRowHeight`, which clips what it holds. */
        val ActionRowChassis = 56.dp

        /** `TaffyAssistantPill`'s one height, in every state it has. */
        val PillHeight = 56.dp
    }
}
