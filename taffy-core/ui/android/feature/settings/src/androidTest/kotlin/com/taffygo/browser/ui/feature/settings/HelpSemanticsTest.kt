// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TaffyProjectContact
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class HelpSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Test
    fun limitsAreNamedAndBothFeedbackRoutesAreOffered() {
        val intents = mutableListOf<HelpIntent>()
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                HelpContent(state = HelpUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.HelpAndFeedback.screenId).assertExists()
        compose.onNodeWithTag(HELP_LIMITS_TEST_TAG).assertExists()
        compose.onNodeWithTag(HELP_LIMIT_ASSISTANT_TEST_TAG).assertExists()
        compose.onNodeWithTag(HELP_LIMIT_BLOCKING_TEST_TAG).assertExists()
        compose.onNodeWithTag(HELP_LIMIT_SECRETS_TEST_TAG).assertExists()
        compose.onNodeWithTag(HELP_FEEDBACK_EMAIL_TEST_TAG).performScrollTo().assertIsEnabled()
        compose.onNodeWithText(context.getString(R.string.taffy_help_feedback_issue_public))
            .performScrollTo()
            .assertExists()
        compose.onNodeWithTag(HELP_FEEDBACK_NO_EMAIL_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(HELP_FEEDBACK_ISSUE_TEST_TAG).performScrollTo().performClick()

        assertEquals(listOf<HelpIntent>(HelpIntent.OpenPublicIssue), intents)
    }

    @Test
    fun aDraftNoEmailAppTookNamesTheAddress() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                HelpContent(state = HelpUiState(emailUnavailable = true), onIntent = {})
            }
        }

        compose.onNodeWithText(
            context.getString(
                R.string.taffy_help_feedback_no_email_app,
                TaffyProjectContact.EMAIL,
            ),
        ).performScrollTo().assertExists()
    }
}
