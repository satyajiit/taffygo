// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.internal.DefaultBrowserRepository
import com.taffygo.browser.ui.core.model.AddressBarCommand
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Where the address bar is allowed to send a device.
 *
 * `AddressBarResolverTest` proves what typed text *means*. This proves what the
 * repository *does* with the meaning, which is the half that reaches a web
 * engine — and the half that was wrong: a search used to navigate to
 * `search.example.test`, a name under the `.test` domain RFC 6761 reserves, so
 * every real query became a name-resolution failure on a host nobody typed.
 *
 * A location still has to come from the typed text or the narrow suggestion
 * source. A search opens the address [SearchEngineRepository] produces, never
 * a host this class invented.
 */
class DefaultBrowserRepositoryTest {

    @Test
    fun `a search opens the selected engine's address, carrying the query`() = runTest {
        val mediator = RecordingBrowserMediator()
        val search = MemorySearchEngineRepository()
        val repository = DefaultBrowserRepository(mediator, search)

        repository.commit(AddressBarInterpretation.Search("toffee recipe"))

        assertEquals(1, mediator.navigations.size)
        val opened = mediator.navigations.single()
        assertTrue(opened, opened.startsWith("https://www.google.com/search?q="))
        assertTrue(opened, opened.contains("toffee"))
        assertTrue(opened, opened.contains("recipe"))
        assertNull(repository.notice.value)
    }

    @Test
    fun `a blank search does not navigate and does not complain`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        repository.commit(AddressBarInterpretation.Search("   "))

        assertEquals(emptyList<String>(), mediator.navigations)
        assertNull(repository.notice.value)
    }

    @Test
    fun `changing the engine changes where a search goes`() = runTest {
        val mediator = RecordingBrowserMediator()
        val search = MemorySearchEngineRepository()
        val repository = DefaultBrowserRepository(mediator, search)

        search.select(SearchEngineId.DUCKDUCKGO)
        repository.commit(AddressBarInterpretation.Search("toffee"))

        assertTrue(
            mediator.navigations.toString(),
            mediator.navigations.single().startsWith("https://duckduckgo.com/?q="),
        )
    }

    @Test
    fun `a regional duckduckgo search still opens duckduckgo`() = runTest {
        val mediator = RecordingBrowserMediator()
        val search = MemorySearchEngineRepository()
        val repository = DefaultBrowserRepository(mediator, search)

        search.select(SearchEngineId.DUCKDUCKGO_DE)
        repository.commit(AddressBarInterpretation.Search("toffee"))

        assertTrue(
            mediator.navigations.toString(),
            mediator.navigations.single().startsWith("https://duckduckgo.com/?q="),
        )
    }

    @Test
    fun `no search interpretation can produce a reserved test-domain host`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        // The interpretation line on screen SCR-103 is tappable, so any text at
        // all can arrive here as a search — including text that looks exactly
        // like one of the fixture hosts this class used to hold.
        for (text in SEARCHES + "docs.example.test" + "search.example.test" + "") {
            repository.commit(AddressBarInterpretation.Search(text))
        }
        // One real location as well, so the filter below is reading a list with
        // something in it rather than passing because there was nothing to read.
        repository.commit(AddressBarInterpretation.GoTo("en.wikipedia.org", "en.wikipedia.org"))

        assertEquals(7, mediator.navigations.size)
        assertEquals("en.wikipedia.org", mediator.navigations.last())
        assertTrue(
            mediator.navigations.toString(),
            mediator.navigations.dropLast(1).all {
                it.startsWith("https://www.google.com/search?q=")
            },
        )
    }

    @Test
    fun `a search that cannot produce an address says so rather than saying nothing`() = runTest {
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(canSearch = false),
        )

        assertNull(repository.notice.value)
        repository.commit(AddressBarInterpretation.Search("toffee recipe"))

        assertEquals(BrowserNotice.NO_SEARCH_ENGINE, repository.notice.value)
    }

    @Test
    fun `reading the notice clears it`() = runTest {
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(canSearch = false),
        )

        repository.commit(AddressBarInterpretation.Search("toffee recipe"))
        repository.dismissNotice()

        assertNull(repository.notice.value)
    }

    @Test
    fun `going somewhere clears a notice about somewhere else`() = runTest {
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(canSearch = false),
        )

        repository.commit(AddressBarInterpretation.Search("toffee recipe"))
        repository.commit(repository.resolve("en.wikipedia.org"))

        // The person moved on, so the words about the last refusal are no
        // longer about anything in front of them.
        assertNull(repository.notice.value)
    }

    @Test
    fun `a question and a request for work leave no notice behind either`() = runTest {
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(canSearch = false),
        )

        repository.commit(AddressBarInterpretation.Search("toffee recipe"))
        repository.commit(repository.resolve("is this fee refundable?"))
        assertNull(repository.notice.value)

        repository.commit(AddressBarInterpretation.Search("toffee recipe"))
        repository.commit(repository.resolve("compare these 4 TV tabs"))
        assertNull(repository.notice.value)
    }

    @Test
    fun `a location preserves the complete address that was typed`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        val address = "https://en.wikipedia.org/wiki/Toffee?from=taffy#history"
        repository.commit(repository.resolve(address))

        assertEquals(listOf(address), mediator.navigations)
    }

    @Test
    fun `commit rechecks a location instead of trusting presentation state`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        repository.commit(
            AddressBarInterpretation.GoTo(
                "javascript:alert(document.domain)",
                "example.com",
            ),
        )
        repository.commit(
            AddressBarInterpretation.GoTo(
                "https://example.com/private",
                "different.example",
            ),
        )

        assertEquals(emptyList<String>(), mediator.navigations)
    }

    @Test
    fun `a question and a request for work still commit to nothing`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        repository.commit(repository.resolve("is this fee refundable?"))
        repository.commit(repository.resolve("compare these 4 TV tabs"))

        assertEquals(emptyList<String>(), mediator.navigations)
    }

    @Test
    fun `only the explicitly enumerated exact browser commands resolve as commands`() {
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(),
        )
        val expected = linkedMapOf(
            "open history" to AddressBarCommand.OPEN_HISTORY,
            "open bookmarks" to AddressBarCommand.OPEN_BOOKMARKS,
            "open downloads" to AddressBarCommand.OPEN_DOWNLOADS,
            "open settings" to AddressBarCommand.OPEN_SETTINGS,
            "clear browsing data" to AddressBarCommand.OPEN_CLEAR_BROWSING_DATA,
        )

        expected.forEach { (typed, command) ->
            assertEquals(
                AddressBarInterpretation.BrowserCommand(typed, command),
                repository.resolve(typed),
            )
        }
        listOf(
            "open passwords",
            "open history now",
            "delete history",
            "clear all data",
        ).forEach { typed ->
            assertTrue(typed, repository.resolve(typed) !is AddressBarInterpretation.BrowserCommand)
        }
    }

    @Test
    fun `browser commands never execute through the navigation repository`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        AddressBarCommand.entries.forEach { command ->
            repository.commit(AddressBarInterpretation.BrowserCommand(command.name, command))
        }

        assertEquals(emptyList<String>(), mediator.navigations)
    }

    @Test
    fun `command completions are bounded explicit and stable`() {
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(),
        )

        val commands = repository.suggestions("open ")
            .filter { it.source == Suggestion.Source.BROWSER_COMMAND }
            .map { (it.interpretation as AddressBarInterpretation.BrowserCommand).command }

        assertEquals(
            listOf(
                AddressBarCommand.OPEN_HISTORY,
                AddressBarCommand.OPEN_BOOKMARKS,
                AddressBarCommand.OPEN_DOWNLOADS,
                AddressBarCommand.OPEN_SETTINGS,
            ),
            commands,
        )
    }

    @Test
    fun `a typed location is the only GoTo the repository visits`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        repository.commit(repository.resolve("docs.example.test"))

        assertEquals(listOf("docs.example.test"), mediator.navigations)
        assertTrue(
            mediator.navigations.toString(),
            mediator.navigations.all { "docs.example.test".contains(it, ignoreCase = true) },
        )
    }

    @Test
    fun `with no indexed pages there is no site to suggest`() {
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(),
        )

        val suggested = repository.suggestions("doc").map { it.interpretation }

        // Every remaining row is a reading of the typed text itself. Nothing
        // completes `doc` to a host, because this build knows of no host.
        assertTrue(suggested.toString(), suggested.none { it is AddressBarInterpretation.GoTo })
        assertTrue(suggested.toString(), suggested.all { it.input == "doc" })
    }

    @Test
    fun `page suggestions come only from the narrow preindexed source`() {
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(),
            StaticAddressBarSuggestionSource(
                AddressBarPageSuggestion(
                    id = "tab-tab_1",
                    title = "Encyclopedia",
                    address = "https://en.wikipedia.org/wiki/Toffee",
                    host = "en.wikipedia.org",
                    source = Suggestion.Source.OPEN_TAB,
                ),
            ),
        )

        assertEquals(listOf("en.wikipedia.org"), suggestedHosts(repository, "en"))
        assertEquals(
            "https://en.wikipedia.org/wiki/Toffee",
            repository.suggestions("en")
                .map { it.interpretation }
                .filterIsInstance<AddressBarInterpretation.GoTo>()
                .single()
                .input,
        )
    }

    @Test
    fun `unsafe mismatched and non-page adapter rows are rejected`() {
        val source = StaticAddressBarSuggestionSource(
            AddressBarPageSuggestion(
                id = "active",
                title = "Active content",
                address = "javascript:alert(document.domain)",
                host = "example.org",
                source = Suggestion.Source.HISTORY,
            ),
            AddressBarPageSuggestion(
                id = "mismatch",
                title = "Wrong host",
                address = "https://different.example/page",
                host = "example.org",
                source = Suggestion.Source.BOOKMARK,
            ),
            AddressBarPageSuggestion(
                id = "wrong-source",
                title = "Not a page source",
                address = "https://example.org/page",
                host = "example.org",
                source = Suggestion.Source.BROWSER_COMMAND,
            ),
        )
        val repository = DefaultBrowserRepository(
            RecordingBrowserMediator(),
            MemorySearchEngineRepository(),
            source,
        )

        assertEquals(emptyList<String>(), suggestedHosts(repository, "ex"))
    }

    @Test
    fun `a mark request reaches the mediator as it was asked`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        repository.requestSiteMarks(listOf("en.wikipedia.org", "example.org"))

        // Pass-through, not policy: which hosts to ask about is the caller's
        // question, and which marks exist is the engine's answer. This layer
        // adds nothing between them.
        assertEquals(listOf(listOf("en.wikipedia.org", "example.org")), mediator.markRequests)
    }

    @Test
    fun `filtering commands reach the mediator as they were asked`() = runTest {
        val mediator = RecordingBrowserMediator()
        val repository = DefaultBrowserRepository(mediator, MemorySearchEngineRepository())

        repository.setFilteringEnabled(false)
        assertTrue(
            repository.setSiteFilteringException(
                "news.example.test",
                allow = true,
                plane = SiteFilteringPlane.SELECTED_TAB,
            ),
        )
        repository.flushFilteringCounts()

        // Pass-through again: whether a host is recordable is the browser's
        // rule, and this layer adds no second copy of it. The plane travels
        // with the command rather than being decided here (decision 0128).
        assertEquals(listOf(false), mediator.filteringToggles)
        assertEquals(
            listOf(Triple("news.example.test", true, SiteFilteringPlane.SELECTED_TAB)),
            mediator.siteExceptions,
        )
        assertEquals(1, mediator.timesFlushedFilteringCounts)
    }

    private fun suggestedHosts(repository: DefaultBrowserRepository, input: String) =
        repository.suggestions(input)
            .map { it.interpretation }
            .filterIsInstance<AddressBarInterpretation.GoTo>()
            .map { it.host }

    private companion object {
        /** Text that resolves to a search rather than to any other reading. */
        val SEARCHES = listOf(
            "98 inch tv price",
            "best coffee grinder",
            "retention policy",
            "wikipedia",
            "   ",
        )
    }
}

private class StaticAddressBarSuggestionSource(
    vararg rows: AddressBarPageSuggestion,
) : AddressBarSuggestionSource {
    private val storedRows = rows.toList()

    override fun suggestions(input: String): List<AddressBarPageSuggestion> = storedRows
}

/**
 * The browser seam, recording rather than acting.
 *
 * `navigateTo` is the only call these tests are about, so it is the only one
 * that records: an assertion on [navigations] is an assertion about every place
 * the repository would send a device.
 */
private class RecordingBrowserMediator(openTabs: List<Tab> = emptyList()) : BrowserMediator {

    /** Every host handed to [navigateTo], in the order it was handed over. */
    val navigations = mutableListOf<String>()

    override val tabs: StateFlow<List<Tab>> = MutableStateFlow(openTabs)

    override val navigation: StateFlow<NavigationState> =
        MutableStateFlow(NavigationState(host = "", title = ""))

    override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())

    override val downloads: StateFlow<List<DownloadRecord>> =
        MutableStateFlow(emptyList<DownloadRecord>())

    override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> =
        MutableStateFlow(emptyMap())

    override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())

    /** Every host list handed to [requestSiteMarks], in call order. */
    val markRequests = mutableListOf<List<String>>()

    override suspend fun requestSiteMarks(hosts: Collection<String>) {
        markRequests += hosts.toList()
    }

    override suspend fun openTab(host: String, isPrivate: Boolean): TabId = TabId("tab_unopened")

    override suspend fun selectTab(id: TabId) = Unit

    override suspend fun closeTab(id: TabId) = Unit

    override suspend fun navigateTo(address: String) {
        navigations += address
    }

    override suspend fun goBack(): Boolean = false

    override suspend fun goForward(): Boolean = false

    override suspend fun reload() = Unit

    override suspend fun performDownloadAction(
        id: DownloadId,
        action: DownloadAction,
    ): Boolean = false

    override val filtering: StateFlow<FilteringSettings> = MutableStateFlow(FilteringSettings())

    /** Every toggle handed over, in call order. */
    val filteringToggles = mutableListOf<Boolean>()

    override suspend fun setFilteringEnabled(enabled: Boolean) {
        filteringToggles += enabled
    }

    /** Every exception command handed over, in call order, with its plane. */
    val siteExceptions = mutableListOf<Triple<String, Boolean, SiteFilteringPlane>>()

    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean {
        siteExceptions += Triple(host, allow, plane)
        return true
    }

    var timesFlushedFilteringCounts = 0
        private set

    override suspend fun flushFilteringCounts() {
        timesFlushedFilteringCounts++
    }
}
