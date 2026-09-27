// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.AddressBarSuggestionSource
import com.taffygo.browser.ui.core.browser.BrowserMediator
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SearchEngineRepository
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * The browsing surfaces' view of the browser.
 *
 * A task interpretation never runs here: [commit] does the two things a
 * browser does immediately — open a location, or search with the chosen
 * engine — and leaves the two that need a preview or an answer to those
 * screens, which is UX spec section 5's rule expressed as code rather than as
 * a comment on a screen.
 *
 * ## A refusal is a result, and it is reported
 *
 * Ask Taffy and a task are handed on to another screen, which is answer enough.
 * A search opens the engine chosen in General. Default is
 * Google (decision 0019). This class does not name a host of its own: it
 * asks [SearchEngineRepository] for the address. Which of those engines may
 * ground a model is still OD-019.
 *
 * When that repository cannot produce an address, [notice] records the
 * refusal so the person is told rather than watching the box close on
 * nothing.
 *
 * ## No host is invented here
 *
 * This class sits between typed text and a real web engine, so every string it
 * hands to [BrowserMediator.navigateTo] is a place a person's device is
 * actually sent. The sources are the text the person typed, a bounded
 * profile adapter, and the engine the person (or the default) chose. This
 * file holds no search host of its own and knows no history or bookmark store.
 *
 * Suggestions are derived from the typed value and a pre-indexed adapter. No
 * store is queried or scanned here while the person is typing.
 */
internal class DefaultBrowserRepository(
    private val mediator: BrowserMediator,
    private val searchEngines: SearchEngineRepository,
    private val suggestionSource: AddressBarSuggestionSource = AddressBarSuggestionSource.NONE,
) : BrowserRepository {

    /**
     * The last refusal, held here because this is where refusing happens.
     *
     * It is not browser state — the mediator knows nothing about it — and it
     * outlives the screen that caused it on purpose: committing a search closes
     * screen SCR-103, so the person is somewhere else by the time they need to
     * be told. This repository is a singleton, so the notice survives that move
     * and is waiting on screen SCR-101 when they arrive.
     */
    private val noticeState = MutableStateFlow<BrowserNotice?>(null)

    override val tabs: StateFlow<List<Tab>> = mediator.tabs
    override val navigation: StateFlow<NavigationState> = mediator.navigation
    override val pageAppearance: StateFlow<PageAppearance> = mediator.pageAppearance
    override val downloads: StateFlow<List<DownloadRecord>> = mediator.downloads
    override val downloadsReady: StateFlow<Boolean> = mediator.downloadsReady
    override val downloadsComplete: StateFlow<Boolean> = mediator.downloadsComplete
    override val downloadsUnavailable: StateFlow<Boolean> = mediator.downloadsUnavailable
    override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = mediator.tabArtwork
    override val siteMarks: StateFlow<Map<String, Bitmap>> = mediator.siteMarks
    override val notice: StateFlow<BrowserNotice?> = noticeState.asStateFlow()
    override val suggestionRevision: StateFlow<Long> = suggestionSource.revision

    override fun resolve(input: String): AddressBarInterpretation =
        BrowserCommandResolver.resolve(input) ?: AddressBarResolver.resolve(input)

    override fun resolveSearchAddress(query: String): String? =
        searchEngines.taskSearchUrl(query)

    override fun suggestions(input: String): List<Suggestion> {
        val trimmed = input.trim()
        if (trimmed.isEmpty()) return emptyList()
        val chosen = resolve(trimmed)
        val rows = ArrayList<Suggestion>(MAX_SUGGESTIONS)
        val seen = mutableSetOf<String>()

        fun add(interpretation: AddressBarInterpretation, source: Suggestion.Source) {
            val key = interpretationKey(interpretation)
            if (!seen.add(key) || rows.size >= MAX_SUGGESTIONS) return
            rows += Suggestion(
                id = suggestionId(interpretation),
                title = interpretation.input,
                interpretation = interpretation,
                source = source,
            )
        }

        add(
            chosen,
            if (chosen is AddressBarInterpretation.BrowserCommand) {
                Suggestion.Source.BROWSER_COMMAND
            } else {
                Suggestion.Source.READING
            },
        )
        BrowserCommandResolver.completions(trimmed).forEach {
            add(it, Suggestion.Source.BROWSER_COMMAND)
        }
        val alternatives = AddressBarResolver.alternatives(trimmed)
        alternatives.forEach { add(it, Suggestion.Source.READING) }
        suggestionSource.suggestions(trimmed).forEach { candidate ->
            if (rows.size >= MAX_SUGGESTIONS) return@forEach
            if (candidate.source !in PAGE_SOURCES) return@forEach
            val checked = AddressBarResolver.resolve(candidate.address)
            if (checked !is AddressBarInterpretation.GoTo || checked.host != candidate.host) {
                return@forEach
            }
            val key = interpretationKey(checked)
            if (!seen.add(key)) return@forEach
            rows += Suggestion(
                id = "page-${candidate.id}",
                title = candidate.title,
                interpretation = checked,
                source = candidate.source,
                supportingText = candidate.host,
            )
        }
        return rows
    }

    override suspend fun commit(interpretation: AddressBarInterpretation) {
        when (interpretation) {
            is AddressBarInterpretation.GoTo -> {
                noticeState.value = null
                val current = AddressBarResolver.resolve(interpretation.input)
                if (
                    current !is AddressBarInterpretation.GoTo ||
                    current.host != interpretation.host
                ) {
                    // An interpretation is presentation state, not authority.
                    // Re-read the exact input at the commit seam so a stale or
                    // hand-constructed row cannot smuggle an active scheme (or
                    // a different host) past the address classifier.
                    return
                }
                // The host is projection metadata for suggestions, favicons and
                // site-level controls. Chromium must receive the complete text
                // the person entered or a path, query and fragment are silently
                // discarded before the navigation ever reaches the engine.
                mediator.navigateTo(interpretation.input)
            }
            // The address comes from the chosen engine, never from a host
            // written in this file. A blank query is a no-op. A missing
            // address is the refusal screen SCR-101 already knows how to say.
            is AddressBarInterpretation.Search -> {
                val url = searchEngines.searchUrl(interpretation.input)
                when {
                    interpretation.input.trim().isEmpty() -> Unit
                    url == null -> noticeState.value = BrowserNotice.NO_SEARCH_ENGINE
                    else -> {
                        noticeState.value = null
                        mediator.navigateTo(url)
                    }
                }
            }
            // Asking answers in the Assistant bar and starting a task needs a
            // preview first. Neither is a navigation, so neither happens here —
            // but both are the person moving on, so a notice left over from an
            // earlier refusal is no longer about anything they are looking at.
            is AddressBarInterpretation.AskTaffy,
            is AddressBarInterpretation.TaskForTaffy,
            is AddressBarInterpretation.BrowserCommand,
            ->
                noticeState.value = null
        }
    }

    override fun dismissNotice() {
        noticeState.value = null
    }

    override suspend fun selectTab(id: TabId) = mediator.selectTab(id)

    override suspend fun selectTaskTab(id: TabId, taskId: String): Boolean =
        mediator.selectTaskTab(id, taskId)

    override suspend fun tabsForTask(taskId: String): Set<TabId> = mediator.tabsForTask(taskId)

    override suspend fun closeTab(id: TabId) = mediator.closeTab(id)

    override suspend fun openTab(host: String, isPrivate: Boolean): TabId =
        mediator.openTab(host, isPrivate)

    override suspend fun openAttributionNotice(): Boolean = mediator.openAttributionNotice()

    override suspend fun goBack(): Boolean = mediator.goBack()

    override suspend fun goForward(): Boolean = mediator.goForward()

    override suspend fun reload() = mediator.reload()

    override suspend fun stopLoading(): Boolean = mediator.stopLoading()

    override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) =
        mediator.performDownloadAction(id, action)

    override suspend fun completedTaskDownloads(taskId: String) =
        mediator.completedTaskDownloads(taskId)

    override suspend fun openTaskDownload(taskId: String, id: DownloadId) =
        mediator.openTaskDownload(taskId, id)

    override suspend fun requestSiteMarks(hosts: Collection<String>) =
        mediator.requestSiteMarks(hosts)

    override val filtering: StateFlow<FilteringSettings> = mediator.filtering

    override suspend fun setFilteringEnabled(enabled: Boolean) =
        mediator.setFilteringEnabled(enabled)

    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = mediator.setSiteFilteringException(host, allow, plane)

    override suspend fun flushFilteringCounts() = mediator.flushFilteringCounts()

    private fun interpretationKey(interpretation: AddressBarInterpretation): String =
        when (interpretation) {
            is AddressBarInterpretation.GoTo -> "go:${interpretation.input}"
            is AddressBarInterpretation.Search -> "search:${interpretation.input.lowercase()}"
            is AddressBarInterpretation.AskTaffy -> "ask:${interpretation.input.lowercase()}"
            is AddressBarInterpretation.TaskForTaffy -> "task:${interpretation.template.name}"
            is AddressBarInterpretation.BrowserCommand -> "command:${interpretation.command.name}"
        }

    private fun suggestionId(interpretation: AddressBarInterpretation): String =
        when (interpretation) {
            is AddressBarInterpretation.GoTo -> "reading-go-to"
            is AddressBarInterpretation.Search -> "reading-search"
            is AddressBarInterpretation.AskTaffy -> "reading-ask"
            is AddressBarInterpretation.TaskForTaffy ->
                "reading-task-${interpretation.template.name.lowercase()}"
            is AddressBarInterpretation.BrowserCommand ->
                "command-${interpretation.command.name.lowercase()}"
        }

    private companion object {
        const val MAX_SUGGESTIONS = 8
        val PAGE_SOURCES = setOf(
            Suggestion.Source.OPEN_TAB,
            Suggestion.Source.BOOKMARK,
            Suggestion.Source.HISTORY,
        )
    }
}
