// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performScrollToNode
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Rule
import org.junit.Test

/**
 * The three served provider collections compose only their visible rows.
 *
 * These fixtures use each Core API collection's exact maximum. A far row must
 * begin outside composition and become reachable through the one outer lazy
 * owner; that proves both virtualization and the absence of a nested vertical
 * list whose off-screen children the outer owner could not address.
 */
class ProviderCollectionVirtualizationSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun connectedProvidersVirtualizeAllNinetySixRows() {
        val rows = List(96) { index -> connectedRow(index) }
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                ConnectedProvidersContent(
                    state = ConnectedProvidersUiState(
                        status = ConnectedProvidersUiState.Status.READY,
                        rows = rows,
                    ),
                    onIntent = {},
                )
            }
        }

        val first = "$CONNECTED_ROW_TEST_TAG_PREFIX${rows.first().providerId}"
        val last = "$CONNECTED_ROW_TEST_TAG_PREFIX${rows.last().providerId}"
        compose.onNodeWithTag(first).assertExists()
        compose.onNodeWithTag(last).assertDoesNotExist()

        compose.onNodeWithTag(CONNECTED_LIST_TEST_TAG).performScrollToNode(hasTestTag(last))

        compose.onNodeWithTag(last).assertExists()
        compose.onNodeWithTag(first).assertDoesNotExist()
    }

    @Test
    fun providerHubVirtualizesAllNinetySixRows() {
        val rows = List(96) { index -> hubRow(index) }
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                ProviderHubContent(
                    state = ProviderHubUiState(
                        status = ProviderHubStatus.READY,
                        sections = listOf(
                            ProviderHubSection(
                                group = ProviderHubGroup.BRING_YOUR_OWN_KEY,
                                rows = rows,
                            ),
                        ),
                        showing = ProviderHubGroup.BRING_YOUR_OWN_KEY,
                    ),
                    onIntent = {},
                )
            }
        }

        val first = "$PROVIDER_ROW_TEST_TAG_PREFIX${rows.first().providerId}"
        val last = "$PROVIDER_ROW_TEST_TAG_PREFIX${rows.last().providerId}"
        compose.onNodeWithTag(first).assertExists()
        compose.onNodeWithTag(last).assertDoesNotExist()

        compose.onNodeWithTag(PROVIDER_HUB_LIST_TEST_TAG).performScrollToNode(hasTestTag(last))

        compose.onNodeWithTag(last).assertExists()
        compose.onNodeWithTag(first).assertDoesNotExist()
    }

    @Test
    fun modelSelectionVirtualizesAllTwoHundredFiftySixRows() {
        val rows = List(256) { index -> modelRow(index) }
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                ModelSelectionContent(
                    state = ModelSelectionUiState(
                        status = ModelSelectionUiState.Status.READY,
                        providerId = PROVIDER_ID,
                        ready = listOf(
                            ModelSelectionUiState.Block(
                                providerId = PROVIDER_ID,
                                providerName = "Provider",
                                locked = false,
                                offer = ProviderRowOffer.Configure,
                                groups = listOf(
                                    ModelSelectionUiState.Group(
                                        capability = ModelCapability.STANDARD,
                                        rows = rows,
                                    ),
                                ),
                                listedCount = rows.size,
                                catalogCount = rows.size,
                            ),
                        ),
                    ),
                    onIntent = {},
                )
            }
        }

        val first = "$MODEL_ROW_TEST_TAG_PREFIX${rows.first().modelId}"
        val last = "$MODEL_ROW_TEST_TAG_PREFIX${rows.last().modelId}"
        compose.onNodeWithTag(first).assertExists()
        compose.onNodeWithTag(last).assertDoesNotExist()

        compose.onNodeWithTag(MODEL_LIST_TEST_TAG).performScrollToNode(hasTestTag(last))

        compose.onNodeWithTag(last).assertExists()
        compose.onNodeWithTag(first).assertDoesNotExist()
    }

    private companion object {
        const val PROVIDER_ID = "provider"

        fun connectedRow(index: Int): ConnectedProviderRow = ConnectedProviderRow(
            providerId = "connected-$index",
            displayName = "Connected $index",
            availability = CredentialAvailability.PRESENT,
            accountLabel = null,
            planLabel = null,
            modelName = null,
            thinking = null,
            ownEndpoint = false,
            carriesStandingChoice = index == 0,
            canSignOut = false,
            signingOut = false,
            offer = ProviderRowOffer.Configure,
        )

        fun hubRow(index: Int): ProviderHubRow = ProviderHubRow(
            providerId = "hub-$index",
            displayName = "Provider $index",
            group = ProviderHubGroup.BRING_YOUR_OWN_KEY,
            offer = ProviderRowOffer.Configure,
            wayIn = ProviderWayIn.KEY,
            signingIn = false,
        )

        fun modelRow(index: Int): ModelSelectionUiState.Row = ModelSelectionUiState.Row(
            providerId = PROVIDER_ID,
            providerName = "Provider",
            modelId = "model-$index",
            displayName = "Model $index",
            capability = ModelCapability.STANDARD,
            reasoning = false,
            readsPictures = false,
            contextWindow = 0uL,
            selected = index == 0,
            asking = false,
            locked = false,
        )
    }
}
