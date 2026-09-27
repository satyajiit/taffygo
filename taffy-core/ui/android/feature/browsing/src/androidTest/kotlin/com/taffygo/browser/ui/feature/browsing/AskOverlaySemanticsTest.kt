// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.activity.ComponentActivity
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.click
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performScrollTo
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-301 as an overlay (decision 0135): the page shows through, the
 * scrim and back close it, and the card carries the start page's box with
 * the page on the question as a chip and the consent for it in a sentence.
 *
 * The stateless frame is rendered around the stateless box, the way the
 * start page's semantics tests render theirs: there is no window behind a
 * preview, so there is no entry to build a view model on.
 */
class AskOverlaySemanticsTest {

    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val docs = Tab(TabId("tab_docs"), "Retention policy", "docs.example.test", isSelected = true)
    private val asking = StartConditions(
        readiness = TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY),
        availability = CoreUiAvailability.READY,
        asksInPlace = true,
    )

    private var dismissed = 0
    private val seen = mutableListOf<AddressBarIntent>()

    private fun show(initial: AddressBarUiState = overPage()) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                var address by remember { mutableStateOf(initial) }
                AskOverlayFrame(onDismiss = { dismissed++ }, scrollContent = false) {
                    val onIntent: (AddressBarIntent) -> Unit = { intent ->
                        seen += intent
                        address = reduceAddressBar(address, intent, ::asks) { emptyList() }
                    }
                    AskComposerLayout(
                        composer = {
                            StartPageComposer(
                                state = address,
                                onIntent = onIntent,
                                rowTestTag = ASK_ADDRESS_TEST_TAG,
                                placeholder = context.getString(R.string.taffy_ask_field_hint_page),
                                showOptions = false,
                            )
                        },
                    ) {
                        StartPageOptionChips(state = address, onIntent = onIntent)
                        StartPageResults(state = address, onIntent = onIntent)
                    }
                }
            }
        }
    }

    private fun overPage() = AddressBarUiState(
        conditions = asking,
        attachedPages = listOf(docs.toAttachedPage()),
        pages = askPagesSnapshot(listOf(docs)),
    )

    private fun asks(input: String): AddressBarInterpretation = AddressBarInterpretation.AskTaffy(input)

    @Test
    fun theCardStandsOverTheScrimWithTheBoxAndThePageOnIt() {
        show()

        compose.onNodeWithTag(ASK_OVERLAY_SCRIM_TEST_TAG).assertExists()
        compose.onNodeWithTag(ASK_OVERLAY_CARD_TEST_TAG).assertIsDisplayed()
        compose.onNodeWithTag(ASK_ADDRESS_TEST_TAG).assertIsDisplayed()
        compose.onNodeWithTag("$START_PAGE_CHIP_TEST_TAG_PREFIX${docs.id.value}").assertIsDisplayed()
    }

    @Test
    fun tappingTheScrimClosesTheOverlay() {
        show()

        val scrim = compose.onNodeWithContentDescription(context.getString(R.string.taffy_ask_overlay_close))
        val scrimBounds = scrim.fetchSemanticsNode().boundsInRoot
        val cardBounds = compose.onNodeWithTag(ASK_OVERLAY_CARD_TEST_TAG).fetchSemanticsNode().boundsInRoot
        assertTrue("The card must leave an exposed scrim strip", cardBounds.left > scrimBounds.left)
        // The scrim fills the window behind the card. Its centre is covered,
        // so a default centre tap would hit the dialog instead of the scrim.
        scrim.performTouchInput {
            click(Offset((cardBounds.left - scrimBounds.left) / 2f, cardBounds.center.y - scrimBounds.top))
        }

        assertEquals(1, dismissed)
    }

    @Test
    fun backClosesTheOverlay() {
        show()

        compose.runOnUiThread { compose.activity.onBackPressedDispatcher.onBackPressed() }
        compose.waitForIdle()

        assertEquals(1, dismissed)
    }

    @Test
    fun aQuestionOverThePageSaysWhatLeavesAndForWhere() {
        show()

        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("what is this page for?")

        compose.onNodeWithTag(START_PAGES_TEST_TAG).assertTextEquals(
            context.getString(
                R.string.taffy_ask_disclosure_direct_compact,
                context.resources.getQuantityString(R.plurals.taffy_count_pages, 1, 1),
            ),
        )
        compose.onNodeWithTag(START_CONSENT_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun takingThePageOffMakesTheQuestionAnErrandThatFindsItsOwnSite() {
        show()
        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("what is this page for?")

        compose.onNodeWithContentDescription(
            context.getString(R.string.taffy_ask_remove_page, "Retention policy"),
        ).performClick()

        compose.onNodeWithTag("$START_PAGE_CHIP_TEST_TAG_PREFIX${docs.id.value}").assertDoesNotExist()
        compose.onNodeWithTag(START_PAGES_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(START_CONSENT_TEST_TAG).assertExists()
    }

    @Test
    fun aClosedPageRefusesTheStartInCaution() {
        show(overPage().copy(attachedPages = listOf(docs.toAttachedPage().copy(closed = true))))

        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("what is this page for?")

        compose.onNodeWithTag(START_REFUSAL_TEST_TAG).assertTextEquals(
            context.getString(R.string.taffy_ask_disclosure_closed),
        )
    }

    /**
     * The refusal about pages is the way to the pages.
     *
     * It said "open it from the Ask sheet", and decision 0135 had already
     * turned the Ask sheet into this overlay — so the one sentence a person
     * gets when a comparison has no pages on it sent them to the surface they
     * were reading it on. There was nothing else on the screen to try: the
     * send is refused, correctly, until pages are named, and nothing named
     * where they are named. The line is now the tap that opens them.
     */
    @Test
    fun theRefusalAboutPagesOpensThePlaceThePagesAreChosen() {
        show(
            AddressBarUiState(
                conditions = asking,
                shape = TaskTemplate.COMPARE_PRODUCTS,
                pages = askPagesSnapshot(listOf(docs)),
            ),
        )
        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("compare these two phones")

        compose.onNodeWithTag(START_REFUSAL_TEST_TAG).assertTextEquals(
            context.getString(R.string.taffy_task_start_refused_wrong_page_count),
        )
        compose.onNodeWithTag(START_REFUSAL_TEST_TAG).performClick()

        assertTrue(
            "the refusal opens the attach sheet: $seen",
            AddressBarIntent.OpenAttachPages in seen,
        )
    }

    @Test
    fun theComposerStaysVisibleWhenALongConversationScrolls() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AskOverlayFrame(onDismiss = {}, scrollContent = false) {
                    AskComposerLayout(composer = {
                        StartPageComposer(
                            state = overPage(),
                            onIntent = {},
                            rowTestTag = ASK_ADDRESS_TEST_TAG,
                            placeholder = context.getString(R.string.taffy_ask_field_hint_page),
                            showOptions = false,
                        )
                    }) {
                        repeat(80) { Text("Conversation line $it") }
                    }
                }
            }
        }
        val before = compose.onNodeWithTag(ASK_ADDRESS_TEST_TAG)
            .assertIsDisplayed().fetchSemanticsNode().boundsInRoot
        compose.onNodeWithText("Conversation line 79").performScrollTo().assertIsDisplayed()
        val after = compose.onNodeWithTag(ASK_ADDRESS_TEST_TAG)
            .assertIsDisplayed().fetchSemanticsNode().boundsInRoot
        assertEquals(before, after)
    }
}
