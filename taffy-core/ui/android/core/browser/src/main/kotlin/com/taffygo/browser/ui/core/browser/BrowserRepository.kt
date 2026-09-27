// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.NavigationState
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

private val NO_REPOSITORY_SUGGESTION_REVISIONS: StateFlow<Long> =
    MutableStateFlow(0L).asStateFlow()
private val INCOMPLETE_DOWNLOAD_PROJECTION: StateFlow<Boolean> =
    MutableStateFlow(false).asStateFlow()

/**
 * Everything the browsing surfaces need, over the browser mediator.
 *
 * The address-bar resolution lives here rather than in a screen because it is
 * the rule UX spec section 5 states — one box, four content readings plus
 * exact safe browser commands, with the interpretation shown before anything
 * consequential runs — and a rule that lives in a screen is a rule the next
 * screen gets wrong.
 */
interface BrowserRepository {

    /** Every open tab. */
    val tabs: StateFlow<List<Tab>>

    /** What the selected tab is showing. */
    val navigation: StateFlow<NavigationState>

    /** How the selected page should colour the window and its chrome. */
    val pageAppearance: StateFlow<PageAppearance>

    /** The browser's bounded, newest-first download projection. */
    val downloads: StateFlow<List<DownloadRecord>>

    /** Whether the browser has answered the initial profile download query. */
    val downloadsReady: StateFlow<Boolean>
        get() = INCOMPLETE_DOWNLOAD_PROJECTION

    /** True only after the browser proved the bounded projection saw the whole profile store. */
    val downloadsComplete: StateFlow<Boolean>
        get() = INCOMPLETE_DOWNLOAD_PROJECTION

    /** True when the browser's initial profile-store read failed rather than merely being partial. */
    val downloadsUnavailable: StateFlow<Boolean>
        get() = INCOMPLETE_DOWNLOAD_PROJECTION

    /**
     * Local engine bitmaps for the tab switcher, keyed by tab.
     *
     * Empty until the engine has a favicon or a snapshot. A missing entry is
     * the same as [TabArtwork] with both fields null.
     */
    val tabArtwork: StateFlow<Map<TabId, TabArtwork>>

    /**
     * The engine's stored favicon per host, for sites with no open tab.
     *
     * Fed by [requestSiteMarks]; read by the start page's frequent-sites
     * grid. See [BrowserMediator.siteMarks] — the marks are the profile's own
     * favicon store, never a network fetch.
     */
    val siteMarks: StateFlow<Map<String, Bitmap>>

    /**
     * The last thing the browser was asked to do and did not, or null.
     *
     * [commit] can refuse, and a refusal nobody is told about is a broken
     * control. This is how the refusal reaches a screen: the fact only, in
     * TaffyGo's vocabulary rather than in words, because which words a person
     * reads is the screen's decision and not this layer's.
     */
    val notice: StateFlow<BrowserNotice?>

    /** What typed input means: one content reading or one exact safe browser command. */
    fun resolve(input: String): AddressBarInterpretation

    /**
     * Resolves a query through the person's currently selected engine without
     * navigating. Browser-owned task search uses this twice: policy binds the
     * first exact address and dispatch refuses if the preference changed.
     *
     * This is the task's address and not the person's, so it asks the engine
     * for English — see [SearchEngineCatalog.taskSearchUrl]. Both task call
     * sites go through this one function on purpose: the second is an equality
     * check against the first, and two spellings of "the search address" would
     * make that check refuse every search.
     */
    fun resolveSearchAddress(query: String): String? = null

    /** Bounded rows from readings, commands, open tabs, bookmarks, and history. */
    fun suggestions(input: String): List<Suggestion>

    /** Changes whenever [suggestions] may answer differently for unchanged input. */
    val suggestionRevision: StateFlow<Long>
        get() = NO_REPOSITORY_SUGGESTION_REVISIONS

    /** Act on a resolved interpretation that runs immediately. */
    suspend fun commit(interpretation: AddressBarInterpretation)

    /** The person has read the [notice]. Nothing else clears it by itself. */
    fun dismissNotice()

    /** Select a tab. */
    suspend fun selectTab(id: TabId)

    /** Select only if the browser still owns this live regular tab for the named task. */
    suspend fun selectTaskTab(id: TabId, taskId: String): Boolean = false

    /** Exact regular tabs the browser currently admits as sources for this task. */
    suspend fun tabsForTask(taskId: String): Set<TabId> = emptySet()

    /** Close a tab. */
    suspend fun closeTab(id: TabId)

    /** Open a new tab. */
    suspend fun openTab(host: String, isPrivate: Boolean = false): TabId

    /**
     * Open the package's third-party notices in a new tab. Takes no address: see
     * [BrowserMediator.openAttributionNotice]. False means nothing was opened.
     */
    suspend fun openAttributionNotice(): Boolean = false

    /** Go back, or say there is nowhere to go. */
    suspend fun goBack(): Boolean

    /** Go forward, or say there is nowhere to go. */
    suspend fun goForward(): Boolean

    /** Ask for the page again. */
    suspend fun reload()

    /** Stop the selected tab's current load. False means there was no live load to stop. */
    suspend fun stopLoading(): Boolean = false

    /**
     * Perform one action the current download snapshot explicitly permits.
     *
     * A Boolean result is mandatory so a stale row cannot be reported as accepted merely because
     * an implementation received a fire-and-forget pause request.
     */
    suspend fun performDownloadAction(id: DownloadId, action: DownloadAction): Boolean

    /** Current safe completed files that this exact task started in this browser session. */
    suspend fun completedTaskDownloads(taskId: String): List<DownloadRecord> = emptyList()

    /** Rechecks attribution and file state, then hands the person's Open request to Chromium. */
    suspend fun openTaskDownload(taskId: String, id: DownloadId): Boolean = false

    /** Ask the local favicon store for the marks of [hosts]. See [siteMarks]. */
    suspend fun requestSiteMarks(hosts: Collection<String>)

    /**
     * The profile's ad- and tracker-blocking configuration and lifetime
     * total, for screen SCR-206. The selected page's live facts ride
     * [navigation] with the rest of the page's state.
     */
    val filtering: StateFlow<FilteringSettings>

    /** Turn blocking on or off for the whole profile. */
    suspend fun setFilteringEnabled(enabled: Boolean)

    /**
     * Record or remove one site exception, from the site sheet or the
     * settings screen. [host] is a host, never an address; false means the
     * browser refused to record it and nothing changed.
     *
     * [plane] says which profile's list is written, and it has no default:
     * the site sheet means the selected tab's own plane, while SCR-206 always
     * means the regular profile's (decision 0128).
     */
    suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean

    /**
     * Publish the selected tab's held blocked count now — the site sheet is
     * opening and the number is about to be read.
     */
    suspend fun flushFilteringCounts()
}
