// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import androidx.test.platform.app.InstrumentationRegistry
import androidx.lifecycle.ViewModelStore
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import kotlinx.coroutines.flow.MutableStateFlow
import org.junit.Rule
import org.junit.Test

/** Add pages sheet: search, ticks, caution, confirm. */
class AttachPagesSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<AttachPagesIntent>()

    @Test
    fun openTabsAreNamedAndAlreadyAttachedArriveTicked() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AttachPagesContent(
                    state = PreviewStates.attachPages,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_attach_pages_body)).assertExists()
        compose.onNodeWithTag(ATTACH_PAGES_SEARCH_TEST_TAG).assertExists()
        compose.onNodeWithTag("${ATTACH_PAGES_ROW_TEST_TAG_PREFIX}tab_docs")
            .assertExists()
            .assertIsSelected()
        compose.onNodeWithTag("${ATTACH_PAGES_ICON_TEST_TAG_PREFIX}tab_docs", useUnmergedTree = true)
            .assertIsDisplayed()
        compose.onNodeWithTag("${ATTACH_PAGES_ROW_TEST_TAG_PREFIX}tab_shop")
            .assertExists()
            .performClick()

        assertEquals(listOf(AttachPagesIntent.Toggle(TabId("tab_shop"))), intents)
    }

    @Test
    fun localMarksRefreshEligibleRowsWithoutLosingSelection() {
        val page = Tab(TabId("page"), "A page", "page.example.test")
        val other = Tab(TabId("other"), "Another page", "other.example.test")
        val requested = mutableListOf<Set<String>>()
        val repository = object : AskPagesRepository {
            override val tabs = MutableStateFlow(listOf(
                page, other,
                Tab(TabId("private"), "Private", "private.example.test", isPrivate = true),
                Tab(TabId("taffy"), "Taffy", "task.example.test", isTaffyTab = true),
            ))
            override val tabArtwork = MutableStateFlow<Map<TabId, TabArtwork>>(emptyMap())
            override val siteMarks = MutableStateFlow<Map<String, Bitmap>>(emptyMap())
            override suspend fun requestSiteMarks(hosts: Set<String>) { requested += hosts }
        }
        val viewModel = AttachPagesViewModel(repository)
        val store = ViewModelStore().apply { put("picker", viewModel) }
        val pageMark = Bitmap.createBitmap(2, 2, Bitmap.Config.ARGB_8888)
        val fallbackMark = Bitmap.createBitmap(2, 2, Bitmap.Config.ARGB_8888)
        try {
            compose.setContent {
                val state by viewModel.state.collectAsState()
                LaunchedEffect(Unit) { viewModel.start(listOf(page.id)) }
                TaffyPreview(darkTheme = false, reducedMotion = true) {
                    AttachPagesContent(state, viewModel::onIntent)
                }
            }
            compose.runOnIdle {
                assertEquals(listOf(setOf(page.host, other.host)), requested)
                repository.siteMarks.value = mapOf(page.host to fallbackMark, other.host to fallbackMark)
                repository.tabArtwork.value = mapOf(page.id to TabArtwork(favicon = pageMark))
            }
            compose.runOnIdle {
                assertEquals(listOf(page.id, other.id), viewModel.state.value.rows.map { it.tabId })
                assertSame(pageMark, viewModel.state.value.rows.first().favicon)
                assertSame(fallbackMark, viewModel.state.value.rows.last().favicon)
            }
            compose.onNodeWithTag("$ATTACH_PAGES_ROW_TEST_TAG_PREFIX${page.id.value}").assertIsSelected()
            compose.onNodeWithTag("$ATTACH_PAGES_ICON_TEST_TAG_PREFIX${other.id.value}", useUnmergedTree = true)
                .assertIsDisplayed()
        } finally {
            compose.runOnIdle { store.clear() }
        }
    }

    @Test
    fun confirmNamesTheTickedCount() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AttachPagesContent(
                    state = PreviewStates.attachPages,
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithText(context.getString(R.string.taffy_attach_pages_use)).assertExists()
        compose.onNodeWithTag(ATTACH_PAGES_CONFIRM_TEST_TAG).performClick()

        assertEquals(listOf(AttachPagesIntent.Confirm), intents)
    }

    @Test
    fun moreThanEightShowsACautionAndStillConfirms() {
        val many = (1..9).map { index ->
            AttachPagesUiState.Row(
                tabId = TabId("tab_$index"),
                title = "Page $index",
                host = "site$index.example.test",
                ticked = true,
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                AttachPagesContent(
                    state = AttachPagesUiState(
                        status = AskPagesSnapshot.Status.READY,
                        rows = many,
                        tickedIds = many.map { it.tabId }.toSet(),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(ATTACH_PAGES_CAUTION_TEST_TAG)
            .assertExists()
            .assertIsDisplayed()
        compose.onNodeWithTag(ATTACH_PAGES_CONFIRM_TEST_TAG).performClick()

        assertEquals(listOf(AttachPagesIntent.Confirm), intents)
    }

    @Test
    fun aFullTabProfileComposesOnlyVisibleRows() {
        val rows = (0 until 256).map { index ->
            AttachPagesUiState.Row(
                tabId = TabId("large_$index"),
                title = "Page $index",
                host = "site-$index.example.test",
                ticked = false,
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AttachPagesContent(
                    state = AttachPagesUiState(
                        status = AskPagesSnapshot.Status.READY,
                        rows = rows,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${ATTACH_PAGES_ROW_TEST_TAG_PREFIX}large_255")
            .assertDoesNotExist()
        compose.onNodeWithTag(ATTACH_PAGES_LIST_TEST_TAG).performScrollToIndex(rows.lastIndex)
        compose.onNodeWithTag("${ATTACH_PAGES_ROW_TEST_TAG_PREFIX}large_255").assertExists()
        compose.onNodeWithTag("${ATTACH_PAGES_ROW_TEST_TAG_PREFIX}large_0")
            .assertDoesNotExist()
    }

    @Test
    fun loadingShowsDescribedSkeletons() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AttachPagesContent(
                    state = AttachPagesUiState(status = AskPagesSnapshot.Status.LOADING),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithContentDescription(
            context.getString(R.string.taffy_attach_pages_loading),
        ).assertExists()
        compose.onNodeWithTag(ATTACH_PAGES_CONFIRM_TEST_TAG).assertDoesNotExist()
    }
}
