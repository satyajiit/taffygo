// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.AddressBarSuggestionSource
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Privacy, freshness, ordering, and bounds at the local-page suggestion seam. */
@OptIn(ExperimentalCoroutinesApi::class)
class ProfileAddressBarSuggestionSourceTest {

    @Test
    fun `normal suggestions combine pages in stable source order and deduplicate destinations`() =
        runTest {
            val source = source(
                tabs = listOf(tab("open", "Open docs", "docs.example.test", selected = true)),
                bookmarks = listOf(
                    bookmark(
                        id = "saved",
                        title = "Policy manual",
                        host = "docs.example.test",
                        address = "https://docs.example.test/policy",
                    ),
                ),
                history = listOf(
                    visit(
                        id = "duplicate",
                        title = "Policy manual",
                        host = "docs.example.test",
                        address = "https://docs.example.test/policy",
                        visitedAt = 20,
                    ),
                    visit(
                        id = "history",
                        title = "Docs archive",
                        host = "history.example.test",
                        address = "https://history.example.test/docs",
                        visitedAt = 10,
                    ),
                ),
            )
            runCurrent()

            assertEquals(
                listOf(
                    Suggestion.Source.OPEN_TAB,
                    Suggestion.Source.BOOKMARK,
                    Suggestion.Source.HISTORY,
                ),
                source.adapter.suggestions("doc").map { it.source },
            )
            assertEquals(
                listOf("bookmark-saved"),
                source.adapter.suggestions("policy").map { it.id },
            )
        }

    @Test
    fun `private mode exposes only private open tabs`() = runTest {
        val source = source(
            tabs = listOf(
                tab("normal", "Public", "public.example.test"),
                tab("private", "Private", "private.example.test", private = true, selected = true),
            ),
            bookmarks = listOf(
                bookmark("saved", "Persistent policy", "saved.example.test"),
            ),
            history = listOf(
                visit("visited", "Persistent visit", "visited.example.test", visitedAt = 30),
                visit(
                    "private-history",
                    "Private stored",
                    "private-history.example.test",
                    visitedAt = 20,
                ).copy(isPrivate = true),
                visit("taffy", "Taffy trail", "trail.example.test", visitedAt = 10)
                    .copy(isTaffyWorkingTrail = true),
            ),
        )
        runCurrent()

        assertEquals(
            listOf("tab-private"),
            source.adapter.suggestions("private").map { it.id },
        )
        assertTrue(source.adapter.suggestions("public").isEmpty())
        assertTrue(source.adapter.suggestions("persistent").isEmpty())

        source.tabs.value = source.tabs.value.map { it.copy(isSelected = false) }
        runCurrent()
        assertTrue(source.adapter.suggestions("persistent").isEmpty())

        source.tabs.value = source.tabs.value.map {
            it.copy(isSelected = it.id == TabId("normal"))
        }
        runCurrent()

        assertEquals(listOf("tab-normal"), source.adapter.suggestions("public").map { it.id })
        assertEquals(
            listOf("bookmark-saved", "history-visited"),
            source.adapter.suggestions("persistent").map { it.id },
        )
        assertTrue(source.adapter.suggestions("private").isEmpty())
        assertTrue(source.adapter.suggestions("taffy").isEmpty())
    }

    @Test
    fun `deleted pages disappear when their snapshot changes`() = runTest {
        val source = source(
            bookmarks = listOf(bookmark("removed", "Remove me", "removed.example.test")),
        )
        runCurrent()
        assertEquals(listOf("bookmark-removed"), source.adapter.suggestions("remove").map { it.id })
        val beforeDeletion = source.adapter.revision.value

        source.bookmarks.value = readyBookmarks(emptyList())
        runCurrent()

        assertTrue(source.adapter.suggestions("remove").isEmpty())
        assertTrue(source.adapter.revision.value > beforeDeletion)
    }

    @Test
    fun `lookup is bounded and repeatable`() = runTest {
        val visits = (0 until 120).map { index ->
            visit(
                id = "visit-$index",
                title = "Site $index",
                host = "site$index.example.test",
                visitedAt = index.toLong(),
            )
        }
        val source = source(history = visits)
        runCurrent()

        val first = source.adapter.suggestions("site")
        val second = source.adapter.suggestions("site")

        assertEquals(AddressBarSuggestionSource.MAX_RESULTS, first.size)
        assertEquals(first, second)
        assertEquals(
            listOf("history-visit-119", "history-visit-118", "history-visit-117"),
            first.take(3).map { it.id },
        )
    }

    private fun TestScope.source(
        tabs: List<Tab> = emptyList(),
        history: List<HistoryVisit> = emptyList(),
        bookmarks: List<Bookmark> = emptyList(),
    ): SourceHarness {
        val tabState = MutableStateFlow(tabs)
        val historyState = MutableStateFlow<HistorySnapshot>(HistorySnapshot.Ready(history))
        val bookmarkState = MutableStateFlow<BookmarksSnapshot>(readyBookmarks(bookmarks))
        return SourceHarness(
            tabs = tabState,
            bookmarks = bookmarkState,
            adapter = ProfileAddressBarSuggestionSource(
                tabs = tabState,
                history = historyState,
                bookmarks = bookmarkState,
                scope = backgroundScope,
            ),
        )
    }

    private fun readyBookmarks(bookmarks: List<Bookmark>): BookmarksSnapshot =
        BookmarksSnapshot.Ready(
            listOf(BookmarkFolder(BookmarkFolder.Id.ALL, name = "", bookmarks = bookmarks)),
        )

    private fun tab(
        id: String,
        title: String,
        host: String,
        private: Boolean = false,
        selected: Boolean = false,
    ) = Tab(TabId(id), title, host, isPrivate = private, isSelected = selected)

    private fun bookmark(
        id: String,
        title: String,
        host: String,
        address: String = "https://$host/page",
    ) = Bookmark(Bookmark.Id(id), title, host, address = address)

    private fun visit(
        id: String,
        title: String,
        host: String,
        address: String = "https://$host/page",
        visitedAt: Long,
    ) = HistoryVisit(
        id = HistoryVisit.Id(id),
        title = title,
        host = host,
        visitedAtEpochMillis = visitedAt,
        address = address,
    )

    private data class SourceHarness(
        val tabs: MutableStateFlow<List<Tab>>,
        val bookmarks: MutableStateFlow<BookmarksSnapshot>,
        val adapter: ProfileAddressBarSuggestionSource,
    )
}
