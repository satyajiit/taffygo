// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-108 — the error page, one per reason.
 *
 * The retry appears only where retrying could plausibly work. A certificate
 * that is not valid does not become valid because someone pressed a button, so
 * that page has no button to press.
 */
class PageFailureSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private var reloads = 0

    @Test
    fun beingOfflineSaysSoAndOffersARetry() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                PageFailureNotice(
                    failure = PageLoadFailure.OFFLINE,
                    onReload = { reloads++ },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_error_offline_title)).assertExists()
        compose.onNodeWithTag(RETRY_TEST_TAG).assertExists().performClick()

        assertEquals(1, reloads)
    }

    @Test
    fun anInvalidCertificateOffersUnderstandingRatherThanARetry() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                PageFailureNotice(
                    failure = PageLoadFailure.CERTIFICATE_INVALID,
                    onReload = { reloads++ },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_error_certificate_title)).assertExists()
        compose.onNodeWithTag(RETRY_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aFailureIsAnnouncedWithoutWaitingToBeReached() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                PageFailureNotice(
                    failure = PageLoadFailure.TIMED_OUT,
                    onReload = { reloads++ },
                )
            }
        }

        compose.onNodeWithTag(FAILURE_TEST_TAG).assert(
            SemanticsMatcher.keyIsDefined(SemanticsProperties.LiveRegion),
        )
    }

    @Test
    fun eachReasonHasItsOwnWordsRatherThanOnePageForAllOfThem() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                PageFailureNotice(
                    failure = PageLoadFailure.NAME_NOT_RESOLVED,
                    onReload = { reloads++ },
                )
            }
        }

        val titles = PageLoadFailure.entries.map { failure ->
            when (failure) {
                PageLoadFailure.OFFLINE -> context.getString(R.string.taffy_error_offline_title)
                PageLoadFailure.NAME_NOT_RESOLVED -> context.getString(R.string.taffy_error_name_title)
                PageLoadFailure.UNREACHABLE ->
                    context.getString(R.string.taffy_error_unreachable_title)
                PageLoadFailure.TIMED_OUT -> context.getString(R.string.taffy_error_timeout_title)
                PageLoadFailure.CERTIFICATE_INVALID ->
                    context.getString(R.string.taffy_error_certificate_title)
                PageLoadFailure.PAGE_CRASHED ->
                    context.getString(R.string.taffy_error_crashed_title)
            }
        }
        assertEquals(titles.size, titles.toSet().size)
        compose.onNodeWithText(context.getString(R.string.taffy_error_name_title)).assertExists()
    }

    @Test
    fun aPageThatStoppedOffersARetry() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                PageFailureNotice(
                    failure = PageLoadFailure.PAGE_CRASHED,
                    onReload = { reloads++ },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_error_crashed_title)).assertExists()
        compose.onNodeWithTag(RETRY_TEST_TAG).assertExists().performClick()

        assertEquals(1, reloads)
    }
}
