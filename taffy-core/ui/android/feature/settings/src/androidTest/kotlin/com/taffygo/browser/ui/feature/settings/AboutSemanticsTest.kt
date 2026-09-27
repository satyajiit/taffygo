// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class AboutSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<AboutIntent>()

    @Test
    fun unknownVersionAndHelpArePresent() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AboutContent(
                    state = AboutUiState(
                        facts = UnavailableAboutRepository().facts.copy(
                            chromiumVersion = "152.0.7977.42",
                        ),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.About.screenId).assertExists()
        compose.onNodeWithTag(ABOUT_VERSION_TEST_TAG).assertExists()
        compose.onNodeWithTag(ABOUT_CHROMIUM_VERSION_TEST_TAG).assertExists()
        compose.onNodeWithTag(ABOUT_HELP_TEST_TAG).assertExists().performClick()
        assertEquals(listOf(AboutIntent.OpenHelp), intents)
    }

    /** Decision 0206: the notices and the source are one row each, and each asks for its page. */
    @Test
    fun licencesAndSourceRowsAskForTheirPages() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AboutContent(
                    state = AboutUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(ABOUT_LICENCES_TEST_TAG).assertExists().performClick()
        compose.onNodeWithTag(ABOUT_SOURCE_TEST_TAG).assertExists().performClick()
        assertEquals(listOf(AboutIntent.OpenLicences, AboutIntent.OpenSource), intents)
    }
}
