// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.content.Intent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class PrivacySemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<PrivacyIntent>()

    @Test
    fun storedRouteAndClearArePresentAndExportStaysDisabledPath() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                PrivacyContent(
                    state = PrivacyUiState(savedWorkspaces = PrivacyDataCount.Known(2)),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.Privacy.screenId).assertExists()
        compose.onNodeWithTag(PRIVACY_STORED_TEST_TAG).assertExists()
        compose.onNodeWithTag(PRIVACY_INVENTORY_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(
                R.string.taffy_privacy_inventory_row,
                context.getString(R.string.taffy_privacy_inventory_workspaces),
                context.getString(R.string.taffy_privacy_inventory_exact, 2),
            ),
        ).assertExists()
        compose.onNodeWithTag(PRIVACY_ROUTE_TEST_TAG).assertExists()
        compose.onNodeWithTag(PRIVACY_CLEAR_TEST_TAG)
            .performScrollTo()
            .assertExists()
            .performClick()
        compose.onNodeWithTag(PRIVACY_EXPORT_DELETE_TEST_TAG)
            .performScrollTo()
            .assertExists()
        assertEquals(listOf(PrivacyIntent.OpenClearData), intents)
    }

    @Test
    fun heroBodyDoesNotRepeatTheStoredHeadingAndRoutesStayInUserWords() {
        var route by mutableStateOf(ProviderRoute.DIRECT_WITH_YOUR_KEY)
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                PrivacyContent(
                    state = PrivacyUiState(route = route),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_privacy_hero_body)).assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_privacy_stored_heading))
            .assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_privacy_route_direct))
            .assertExists()
        // "In user words" against something that exists: the audit label. Two
        // assertDoesNotExist calls stood here naming sentences no string in
        // this tree has ever carried — "Through the managed service." and
        // "This reviewed task does not use a model" — so they passed by
        // describing nothing. A route's compiled label is a real value that
        // really would leak if a body were ever built from the enum instead
        // of from a resource.
        compose.onNodeWithText(ProviderRoute.DIRECT_WITH_YOUR_KEY.label).assertDoesNotExist()

        compose.runOnIdle {
            route = ProviderRoute.NO_MODEL_REQUIRED
        }
        compose.onNodeWithText(context.getString(R.string.taffy_privacy_route_no_model))
            .assertExists()
        compose.onNodeWithText(ProviderRoute.NO_MODEL_REQUIRED.label).assertDoesNotExist()
    }

    @Test
    fun deletionNeedsAnExplicitSecondActionAndShowsOnlyStarting() {
        var state by mutableStateOf(PrivacyUiState(deleteAvailable = true))
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                PrivacyContent(
                    state = state,
                    onIntent = { intent ->
                        intents += intent
                        state = reducePrivacy(state, intent)
                    },
                )
            }
        }

        compose.onNodeWithTag(PRIVACY_DELETE_TEST_TAG).performScrollTo().performClick()
        compose.onNodeWithTag(PRIVACY_DELETE_CONFIRM_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_privacy_delete_external_limit),
        ).assertExists()
        compose.onNodeWithTag(PRIVACY_DELETE_CONFIRM_ACTION_TEST_TAG).performClick()
        compose.onNodeWithText(context.getString(R.string.taffy_privacy_delete_starting))
            .assertExists()
        assertEquals(
            listOf(
                PrivacyIntent.RequestDeleteEverything,
                PrivacyIntent.ConfirmDeleteEverything,
            ),
            intents,
        )
    }

    @Test
    fun exportUsesAndroidsCreateDocumentSurfaceWithAFixedSafeName() {
        val intent = CreateProfileDataExportDocument().createIntent(context, Unit)

        assertEquals(Intent.ACTION_CREATE_DOCUMENT, intent.action)
        assertEquals("application/json", intent.type)
        assertEquals(PROFILE_DATA_FILE_NAME, intent.getStringExtra(Intent.EXTRA_TITLE))
        assertEquals(true, intent.hasCategory(Intent.CATEGORY_OPENABLE))
    }
}
