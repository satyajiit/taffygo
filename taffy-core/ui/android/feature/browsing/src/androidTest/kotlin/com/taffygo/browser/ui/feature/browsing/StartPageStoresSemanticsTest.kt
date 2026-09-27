// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Rule
import org.junit.Test

/**
 * A store attached from the plus (decision 0133): the chip under the box, the
 * sentence under the reading that names it, and the way off again.
 */
class StartPageStoresSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val ready = StartConditions(
        readiness = TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY),
        availability = CoreUiAvailability.READY,
        onBlankTab = true,
    )

    private fun showTypeable() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                var address by remember { mutableStateOf(AddressBarUiState(conditions = ready)) }
                NewTabContent(
                    state = PreviewStates.newTab,
                    onIntent = {},
                    composer = previewStartComposer(address) { intent ->
                        address = reduceAddressBar(address, intent, ::readsAsAnErrand) { emptyList() }
                    },
                )
            }
        }
    }

    private fun tile(store: TaskAttachedStore) = "$START_STORE_TILE_TEST_TAG_PREFIX${store.label}"
    private fun chip(store: TaskAttachedStore) = "$START_STORE_CHIP_TEST_TAG_PREFIX${store.label}"

    @Test
    fun aStoreAttachedFromTheMenuBecomesAChipAndTheConsentNamesIt() {
        showTypeable()
        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("download my aadhaar")

        compose.onNodeWithTag(START_OPTIONS_TEST_TAG).performClick()
        compose.onNodeWithTag(tile(TaskAttachedStore.HISTORY)).performClick()

        compose.onNodeWithTag(chip(TaskAttachedStore.HISTORY)).assertExists()
        compose.onNodeWithTag(chip(TaskAttachedStore.BOOKMARKS)).assertDoesNotExist()
        compose.onNodeWithTag(START_STORES_TEST_TAG).assertTextEquals(
            context.getString(
                R.string.taffy_task_start_stores_one,
                context.getString(R.string.taffy_task_start_store_history),
            ),
        )

        // A second store joins the sentence in store order, whichever was tapped first.
        compose.onNodeWithTag(START_OPTIONS_TEST_TAG).performClick()
        compose.onNodeWithTag(tile(TaskAttachedStore.BOOKMARKS)).performClick()
        compose.onNodeWithTag(START_STORES_TEST_TAG).assertTextEquals(
            context.getString(
                R.string.taffy_task_start_stores_two,
                context.getString(R.string.taffy_task_start_store_history),
                context.getString(R.string.taffy_task_start_store_bookmarks),
            ),
        )
    }

    @Test
    fun theChipsRemoveTakesTheStoreOffAndTheSentenceWithIt() {
        showTypeable()
        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("download my aadhaar")
        compose.onNodeWithTag(START_OPTIONS_TEST_TAG).performClick()
        compose.onNodeWithTag(tile(TaskAttachedStore.OPEN_TABS)).performClick()
        compose.onNodeWithTag(START_STORES_TEST_TAG).assertExists()

        val remove = context.getString(
            R.string.taffy_address_bar_chip_remove,
            context.getString(R.string.taffy_start_store_open_tabs),
        )
        compose.onNodeWithContentDescription(remove).assertHasClickAction().performClick()

        compose.onNodeWithTag(chip(TaskAttachedStore.OPEN_TABS)).assertDoesNotExist()
        compose.onNodeWithTag(START_STORES_TEST_TAG).assertDoesNotExist()
    }

    /** With nothing attached, the consent says nothing about stores at all. */
    @Test
    fun anOrdinaryRequestNamesNoStore() {
        showTypeable()
        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("download my aadhaar")

        compose.onNodeWithTag(START_CONSENT_TEST_TAG).assertExists()
        compose.onNodeWithTag(START_STORES_TEST_TAG).assertDoesNotExist()
    }
}

private fun readsAsAnErrand(input: String): AddressBarInterpretation =
    AddressBarInterpretation.TaskForTaffy(input, TaskTemplate.WEB_ERRAND)
