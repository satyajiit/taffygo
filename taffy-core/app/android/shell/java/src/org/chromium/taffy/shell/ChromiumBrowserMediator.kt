// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.content.Context
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import com.taffygo.browser.ui.core.browser.BrowserMediator
import com.taffygo.browser.ui.core.browser.BrowserProjectionInvalidator
import com.taffygo.browser.ui.core.browser.ErrandPagePort
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Tab as TaffyTab
import com.taffygo.browser.ui.core.model.TabId as TaffyTabId
import java.io.Closeable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import org.chromium.base.lifetime.Destroyable
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tab.TabLaunchType
import org.chromium.chrome.browser.tab_ui.TabContentManager
import org.chromium.chrome.browser.tabmodel.TabClosureParams
import org.chromium.chrome.browser.tabmodel.TabCreatorManager
import org.chromium.chrome.browser.tabmodel.TabModel
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabModelObserver
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver
import org.chromium.components.url_formatter.UrlFormatter
import org.chromium.content_public.browser.LoadUrlParams
import org.chromium.net.NetError
import org.chromium.taffy.host.runAllTeardownOperations
import org.chromium.ui.base.PageTransition
import org.chromium.url.GURL

/**
 * The browser seam, with Chromium's tab model behind it.
 *
 * `BrowserMediator` is the whole of what a Taffy-owned surface may ask of the browser. Every tab
 * here is a tab in `TaffyTabModelHolder`'s selector, every navigation state is the selected tab's,
 * and a command is a call into upstream's own model.
 *
 * ## Threads
 *
 * Every Chromium object named here is main-thread-only, and a `BrowserMediator` command is a
 * `suspend` function a view model may call from any dispatcher. So each command hops with
 * `Dispatchers.Main.immediate` — *immediate*, so a caller already on the main thread pays nothing
 * and a tap does not wait behind the looper's queue. The observers below run on the main thread by
 * construction, and a `MutableStateFlow` write is safe from it.
 *
 * ## Why callbacks invalidate projections rather than refreshing directly
 *
 * Chromium reports one navigation as a burst of URL, title, load and selection callbacks. Each
 * callback names which of the three independently expensive projections became dirty; the
 * invalidator unions a burst and publishes it on the next main-loop turn. There remains exactly one
 * projector for tabs, navigation and filtering, but a title change no longer rebuilds unrelated
 * global filtering statistics or crosses JNI five extra times.
 *
 * Downloads use Chromium's profile-owned offline-content provider through [TaffyDownloadObserver].
 * That adapter owns snapshot/update ordering, private-profile exclusion, and action legality; this
 * mediator publishes its projected records and owns the trusted Android share handoff.
 */
class ChromiumBrowserMediator(
    context: Context,
    profile: Profile,
    private val selector: TabModelSelector,
    private val tabCreators: TabCreatorManager,
    private val startPageUrl: String,
    private val tabContentManager: TabContentManager,
    private val showErrandPage: (String) -> Boolean,
) : BrowserMediator, Destroyable {

    private val tabsState = MutableStateFlow(emptyList<TaffyTab>())
    private val navigationState = MutableStateFlow(TaffyNavigationProjection.nothingShown())
    private val appearanceState = MutableStateFlow(PageAppearance())
    private val artworkState = MutableStateFlow(emptyMap<TaffyTabId, TabArtwork>())
    private val siteMarksState = MutableStateFlow(emptyMap<String, Bitmap>())
    private val tabProjectionCache = StableTabProjectionCache<Tab>(
        idOf = Tab::getId,
        project = { tab, selected ->
            tab.toTaffyTab(
                selected,
                taskTabAttribution?.assistantCreated?.invoke(tab) == true,
                taskTabAttribution?.creatingTaskId?.invoke(tab),
            )
        },
    )
    private var destroyed = false

    private val mainHandler = Handler(Looper.getMainLooper())
    private val projectionInvalidator = BrowserProjectionInvalidator(
        schedule = { task -> mainHandler.post(task) },
        cancel = { task -> mainHandler.removeCallbacks(task) },
        publish = ::refresh,
    )

    /**
     * The filtering planes, and the projection over them.
     *
     * Opening a profile's bridge is [ChromiumFilteringPlanes]' job, not this
     * class's: there are two planes, one per profile, and which one answers is
     * decided by the tab (decision 0128). What arrives here is the projection,
     * which holds no Chromium type at all.
     */
    private val filteringPlanes = ChromiumFilteringPlanes(
        selector = selector,
        onChanged = { projectionInvalidator.filteringAndNavigationChanged() },
        onPrivateTabsEmptied = { projectionInvalidator.navigationChanged() },
    )

    private val filteringProjection get() = filteringPlanes.projection

    /**
     * Browser-owned provenance for task-created tabs in this exact product window.
     *
     * The tab model cannot answer this question: a task tab is an ordinary regular Chromium tab
     * whose ownership was claimed in native before publication. The window runtime binds the one
     * native registry that made that claim. Keeping the reader here means every tab projection
     * asks the authority directly instead of duplicating a second, lossy Kotlin ownership table.
     */
    private var taskTabAttribution: TaskTabSelection.Attribution? = null

    private val siteMarkReader = ChromiumSiteMarkReader(
        schedule = { task -> mainHandler.post(task) },
        cancel = mainHandler::removeCallbacks,
    )
    /**
     * Exact-address creation, activation, search and closure for task-owned
     * regular tabs.
     *
     * Held here rather than wrapped: the window binds this controller straight
     * into [org.chromium.taffy.host.TaffyTaskBrowserActions], and the stored
     * pages that history and bookmarks reopen go through it too. A row of
     * mediator pass-throughs would have added a second name for each of those
     * calls and nothing else — the seam a Taffy-owned surface may use is
     * `BrowserMediator`, and none of these is on it.
     */
    internal val taskTabs = ChromiumTaskTabController(selector, tabCreators, startPageUrl)

    private val downloadsController = ChromiumDownloadController(context, profile, mainHandler)

    /**
     * The last error each tab's current navigation reported, by Chromium tab id.
     *
     * A tab's failure is not a property `Tab` exposes — it arrives once, as a callback argument,
     * and is gone. Holding it here is what lets [navigation] keep describing a failed page after
     * the event that failed it, which is what screen SCR-108 needs. An entry is cleared when the
     * same tab starts loading again, so a retry that succeeds does not keep the old notice up.
     */
    private val lastError = mutableMapOf<Int, Int>()

    override val tabs: StateFlow<List<TaffyTab>> = tabsState.asStateFlow()

    override val navigation: StateFlow<NavigationState> = navigationState.asStateFlow()

    override val pageAppearance: StateFlow<PageAppearance> = appearanceState.asStateFlow()

    override val downloads: StateFlow<List<DownloadRecord>> = downloadsController.downloads

    override val downloadsReady: StateFlow<Boolean> = downloadsController.ready

    /** True only when the bounded projection proved it saw the complete profile store. */
    override val downloadsComplete: StateFlow<Boolean> = downloadsController.complete

    override val downloadsUnavailable: StateFlow<Boolean> = downloadsController.unavailable

    override val tabArtwork: StateFlow<Map<TaffyTabId, TabArtwork>> = artworkState.asStateFlow()

    override val siteMarks: StateFlow<Map<String, Bitmap>> = siteMarksState.asStateFlow()

    override val filtering: StateFlow<FilteringSettings> = filteringProjection.settings

    /**
     * The one page an errand runs on — screen SCR-110.
     *
     * Owned here rather than beside the activity because this is the class that
     * already holds tab creation, the selector, and the filtering plane the
     * pill's badge reads. It is published as its narrow port, so nothing above
     * this layer can reach the tab behind it.
     */
    private val errand = ChromiumErrandPageController(
        selector = selector,
        tabCreators = tabCreators,
        filtering = { tab ->
            Triple(
                filteringProjection.isActiveFor(tab),
                filteringProjection.isExceptedFor(tab),
                filteringProjection.blockedCountFor(tab),
            )
        },
        changed = {
            tabProjectionCache.invalidateAll()
            projectionInvalidator.tabsChanged()
            projectionInvalidator.navigationChanged()
        },
        show = showErrandPage,
    )

    /** Opening, walking and closing the page an errand runs on — screen SCR-110. */
    val errandPage: ErrandPagePort get() = errand

    /** Binds the exact window's native task-tab provenance for the lifetime of that window. */
    fun bindTaskTabAttribution(
        reader: (Tab) -> Boolean,
        taskIdReader: (Tab) -> String,
        sourceReader: (String, Tab) -> Boolean,
    ): Closeable {
        ThreadUtils.assertOnUiThread()
        check(taskTabAttribution == null) { "Task-tab attribution is already bound" }
        taskTabAttribution = TaskTabSelection.Attribution(reader, taskIdReader, sourceReader)
        tabProjectionCache.invalidateAll()
        projectionInvalidator.tabsChanged()

        var closed = false
        return Closeable {
            if (closed) return@Closeable
            ThreadUtils.assertOnUiThread()
            closed = true
            if (taskTabAttribution?.assistantCreated === reader) {
                taskTabAttribution = null
                tabProjectionCache.invalidateAll()
                projectionInvalidator.tabsChanged()
            }
        }
    }

    /** Tabs arriving, leaving and being selected — across both models, including restore. */
    private val modelObserver = object : TabModelSelectorTabModelObserver(selector) {
        override fun didAddTab(
            tab: Tab,
            type: Int,
            creationState: Int,
            markedForSelection: Boolean,
        ) {
            tabProjectionCache.invalidate(tab.id)
            projectionInvalidator.tabsAndNavigationChanged()
        }

        override fun didSelectTab(tab: Tab, type: Int, lastId: Int) {
            // Activation is one of the moments the count is about to be
            // looked at (decision 0076), so the coalescer publishes what it
            // holds before the projection below reads it.
            filteringProjection.flushCountFor(tab)
            projectionInvalidator.tabsAndNavigationChanged()
        }

        override fun didMoveTab(tab: Tab, newIndex: Int, curIndex: Int) =
            projectionInvalidator.tabsChanged()

        override fun didRemoveTabForClosure(tab: Tab) = forget(tab)

        override fun tabRemoved(tab: Tab) = forget(tab)

        override fun tabClosureUndone(tab: Tab) =
            projectionInvalidator.tabsAndNavigationChanged()

        /** The session has finished loading, which is the first moment the tab list is true. */
        override fun restoreCompleted() {
            tabProjectionCache.invalidateAll()
            projectionInvalidator.everythingChanged()
        }
    }

    /** What each tab is doing — across every tab the selector owns, including ones added later. */
    private val tabObserver = object : TabModelSelectorTabObserver(selector) {
        override fun onPageLoadStarted(tab: Tab, url: GURL) {
            // The engine commits chrome-error:// after a failed navigation.
            // That is a new page load to this observer, and clearing here
            // would take TaffyGo's notice down the moment Chromium's own
            // document starts — which is how the unbranded "site can't be
            // reached" page was what a person actually saw.
            if (TaffyNavigationProjection.clearsRememberedError(url.spec)) {
                lastError.remove(tab.id)
            }
            invalidateTab(tab)
        }

        override fun onPageLoadFinished(tab: Tab, url: GURL) = invalidateTab(tab)

        override fun onPageLoadFailed(tab: Tab, errorCode: Int) {
            val existing = lastError[tab.id] ?: TaffyNavigationProjection.NO_ERROR
            val remembered = TaffyNavigationProjection.rememberError(existing, errorCode)
            if (remembered == TaffyNavigationProjection.NO_ERROR) {
                lastError.remove(tab.id)
            } else {
                lastError[tab.id] = remembered
            }
            invalidateNavigation(tab)
        }

        override fun onTitleUpdated(tab: Tab) = invalidateTab(tab)

        override fun onUrlUpdated(tab: Tab) = invalidateTab(tab)

        override fun onLoadStopped(tab: Tab, toDifferentDocument: Boolean) = invalidateNavigation(tab)

        /**
         * A renderer died. The page is gone and nothing is loading, so the surfaces have to be
         * told: without this the address pill would keep describing a page that is no longer
         * there. The sentinel is [TaffyNavigationProjection.PAGE_CRASHED], which is how
         * screen SCR-108 draws TaffyGo's own words over Chromium's sad-tab page.
         */
        override fun onCrash(tab: Tab) {
            lastError[tab.id] = TaffyNavigationProjection.PAGE_CRASHED
            invalidateNavigation(tab)
        }
    }

    private val chromeObserver = TaffyPageChromeObserver(selector) { appearanceState.value = it }

    private val artworkObserver = TaffyTabArtworkObserver(selector, tabContentManager) {
        artworkState.value = it
    }

    init {
        refresh(BrowserProjectionInvalidator.Scope.ALL)
    }

    // -----------------------------------------------------------------------
    // Commands.
    // -----------------------------------------------------------------------

    /**
     * Opens a tab on [host] and selects it.
     *
     * **Never `launchNtp`.** Upstream's new-tab launch resolves `chrome://newtab` through
     * [TaffyTabDelegateFactory.createNativePage], which returns null by design — so the address
     * would be loaded as an ordinary web page and the renderer would answer it with an error.
     * TaffyGo's new tab is screen SCR-104 in Compose, and the page behind it is the start page.
     */
    override suspend fun openTab(host: String, isPrivate: Boolean): TaffyTabId =
        onUiThread {
            val creator = tabCreators.getTabCreator(isPrivate)
            val idsBefore = currentTabIds()
            val opened = creator.launchUrl(addressFor(host), TabLaunchType.FROM_CHROME_UI)
            // Plus must mint a tab even when an unused blank already exists.
            // Upstream may reuse that blank; FROM_LINK with the current tab as
            // parent is the launch type that still inserts a second one.
            val minted =
                if (host.isBlank() && opened != null && opened.id in idsBefore) {
                    creator.createNewTab(
                        LoadUrlParams(startPageUrl),
                        TabLaunchType.FROM_LINK,
                        selector.currentTab,
                    ) ?: opened
                } else {
                    opened
                }
            if (minted == null) NO_TAB else TaffyTabId(minted.id.toString())
        }

    /** The package's notices, by the one entry point that takes no address (decision 0206). */
    override suspend fun openAttributionNotice() = onUiThread { TaffyAttributionNotice.open(tabCreators) }

    override suspend fun selectTab(id: TaffyTabId) {
        onUiThread { TaskTabSelection.selectCurrentTab(selector, findTab(id)) }
    }

    override suspend fun selectTaskTab(id: TaffyTabId, taskId: String): Boolean = onUiThread {
        TaskTabSelection.select(selector, findTab(id), taskId,
            taskTabAttribution?.assistantCreated, taskTabAttribution?.creatingTaskId, taskTabAttribution?.acceptedSource)
    }

    override suspend fun tabsForTask(taskId: String): Set<TaffyTabId> = onUiThread {
        TaskTabSelection.sources(selector, taskId, taskTabAttribution?.acceptedSource)
    }

    /**
     * Closes a tab, opening its replacement first when that was the last one.
     *
     * **The order is the fix, not a tidy-up.** Closing first and replacing afterwards means the
     * browser passes through a state with no tab at all, and everything watching the selected tab
     * sees it: the page host is told to show nothing, which is a real state a browser can be in and
     * which this build previously could not survive — see `TaffyPageRenderView`. Creating the
     * replacement first means the selected tab moves from one live tab to another and that state is
     * never entered. The page host still answers it correctly; this simply stops asking.
     *
     * The condition is the same one it always was, asked one step earlier: "will the regular model
     * be empty once this tab is gone". A private tab closing while no regular tab exists counts,
     * which is why the arithmetic is written out rather than compared to zero.
     *
     * `allowUndo` is false even though the model supports undo, because nothing in this build
     * offers the undo: there is no snackbar and no restore control, so a tab held pending closure
     * would be a tab the person is told is closed, still in the model, still holding a renderer,
     * with no way to bring it back. Closing outright makes the count below true immediately.
     *
     * `allowDialog` is false because the dialog it would show belongs to a `ModalDialogManager`,
     * and the selector was built without one — see [TaffyTabModelHolder].
     */
    override suspend fun closeTab(id: TaffyTabId) {
        onUiThread {
            val tab = findTab(id) ?: return@onUiThread
            val model = selector.getModel(tab.isOffTheRecord)
            val regularTabsAfterClose =
                selector.getModel(/* incognito= */ false).count - if (tab.isOffTheRecord) 0 else 1
            if (regularTabsAfterClose == 0) {
                tabCreators
                    .getTabCreator(/* incognito= */ false)
                    .launchUrl(startPageUrl, TabLaunchType.FROM_CHROME_UI)
            }
            model.tabRemover.closeTabs(
                TabClosureParams.closeTab(tab).allowUndo(false).build(),
                /* allowDialog= */ false,
            )
        }
    }

    /**
     * Navigates the selected tab.
     *
     * The address goes through `UrlFormatter.fixupUrl` first, which is the same function
     * `ChromeTabCreator` puts every new tab's address through: it is what turns `example.test`
     * into `http://example.test/` and what refuses a string that is not an address at all. A
     * refusal here does nothing rather than loading something the person did not type — the
     * address bar's own reading of the input (`AddressBarInterpretation`) is what decides between
     * navigating and searching, and it has already run by the time this is called.
     *
     * An engine page is refused here as well, after the fixup rather than before it, because the
     * fixup is what turns `about:version` into `chrome://version`. Nothing that reaches this
     * function should carry one — the resolver navigates on an allowlist — so the refusal is for
     * the caller that does not exist yet. [addressFor] refuses the same set for the same reason,
     * and between them they are every place in this class where a string becomes a load.
     * [TaffyNavigationProjection.isEngineInternalPage] states which addresses those are and why.
     */
    override suspend fun navigateTo(address: String) {
        onUiThread {
            val tab = selector.currentTab ?: return@onUiThread
            val url = UrlFormatter.fixupUrl(address)
            if (!url.isValid) return@onUiThread
            if (TaffyNavigationProjection.isEngineInternalPage(url.validSpecOrEmpty)) {
                return@onUiThread
            }
            // The person typed it. Upstream's omnibox says the same thing, and the transition type
            // is what history, autocomplete and the back/forward list read to tell a typed address
            // from a followed link.
            val params = LoadUrlParams(url.validSpecOrEmpty, PageTransition.TYPED)
            // The first address typed into a blank tab replaces about:blank
            // rather than pushing a history entry. System back then leaves the
            // site (and the browser) instead of drawing the start body again.
            if (TaffyNavigationProjection.hasBeenNowhere(tab.committedSpec)) {
                params.shouldReplaceCurrentEntry = true
            }
            tab.loadUrl(params)
        }
    }

    override suspend fun goBack(): Boolean = onUiThread {
        val tab = selector.currentTab ?: return@onUiThread false
        if (!tab.canGoBack()) return@onUiThread false
        tab.goBack()
        true
    }

    override suspend fun goForward(): Boolean = onUiThread {
        val tab = selector.currentTab ?: return@onUiThread false
        if (!tab.canGoForward()) return@onUiThread false
        tab.goForward()
        true
    }

    override suspend fun reload() {
        onUiThread { selector.currentTab?.reload() }
    }

    override suspend fun stopLoading(): Boolean = onUiThread {
        val tab = selector.currentTab ?: return@onUiThread false
        if (!tab.isLoading) return@onUiThread false
        tab.stopLoading()
        true
    }

    override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction): Boolean =
        onUiThread { downloadsController.perform(id, action) }

    override suspend fun completedTaskDownloads(taskId: String): List<DownloadRecord> =
        onUiThread { downloadsController.completedForTask(taskId) }

    override suspend fun openTaskDownload(taskId: String, id: DownloadId): Boolean =
        onUiThread { downloadsController.openForTask(taskId, id) }

    override suspend fun setFilteringEnabled(enabled: Boolean) {
        onUiThread { filteringProjection.setEnabled(enabled) }
    }

    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = onUiThread {
        when (plane) {
            SiteFilteringPlane.PROFILE ->
                filteringProjection.setProfileSiteException(host, allow)
            SiteFilteringPlane.SELECTED_TAB -> selector.currentTab?.let { tab ->
                filteringProjection.setSiteException(tab, host, allow)
            } ?: false
        }
    }

    override suspend fun flushFilteringCounts() {
        onUiThread { selector.currentTab?.let(filteringProjection::flushCountFor) }
    }

    /**
     * Asks the profile's favicon store for each host's mark, host-fallback on, never the network.
     *
     * This is the same store `TabFavicon` writes as pages load, read back through
     * `favicon_service` — so a site the person visited keeps its mark after its tab closes and
     * across restarts, which is exactly the case the start page's frequent-sites grid is in.
     * The regular model's profile is asked even for nothing-yet states, and only the regular
     * one: a frequent site is never a private tab's site by recording rule, and a private
     * profile's marks must not surface on the start page anyway.
     *
     * A host with a mark already published is not asked again; a host the store had no answer
     * for is retried on the next call, because the store gains marks as the person browses. The
     * The reader asks for a small display-sized mark and keeps a bounded LRU. A long-running
     * profile therefore does not retain every host it has ever shown or repeatedly copy a growing
     * bitmap map as asynchronous answers land.
     */
    override suspend fun requestSiteMarks(hosts: Collection<String>) {
        onUiThread {
            val profile = selector.getModel(false).profile ?: return@onUiThread
            siteMarkReader.request(profile, hosts) { marks -> siteMarksState.value = marks }
        }
    }

    /** Stops observing. The tab model itself belongs to [TaffyTabModelHolder]. */
    override fun destroy() {
        if (destroyed) return
        destroyed = true
        projectionInvalidator.close()
        tabProjectionCache.clear()
        taskTabAttribution = null
        lastError.clear()
        runAllTeardownOperations(
            listOf<() -> Unit>(
                { errand.destroy() },
                { downloadsController.destroy() },
                { filteringPlanes.close() },
                { siteMarkReader.destroy() },
                { artworkObserver.destroy() },
                { chromeObserver.destroy() },
                { tabObserver.destroy() },
                { modelObserver.destroy() },
            ),
        )
    }

    // -----------------------------------------------------------------------
    // Projection.
    // -----------------------------------------------------------------------

    /** Rebuilds exactly the dirty flows from the selector; the only writer of each. */
    private fun refresh(scope: BrowserProjectionInvalidator.Scope) {
        if (destroyed) return
        val openedFilteringBridge = filteringProjection.ensureFilteringBridge()
        val needsCurrent = scope.tabs || scope.navigation || openedFilteringBridge
        // The errand page is a real tab in the real model — it has to be, or the
        // account plane's sign-in could never come back (see
        // [ChromiumErrandPageController]) — so it is excluded here rather than
        // never being there. Excluded from *both* projections and not only the
        // list: an errand tab dropped from `tabs` while `navigation` still
        // described it would be a snapshot with no selected tab and a page
        // anyway, and the vendor's address would be readable through a seam
        // screen SCR-110 exists to keep it out of.
        val selected = if (needsCurrent) selector.currentTab else null
        val current = if (selected != null && errand.isErrandTab(selected)) null else selected
        if (scope.tabs) {
            tabsState.value = tabProjectionCache.snapshot(
                selector.models.currentTabs().filterNot(errand::isErrandTab),
                current?.id,
            )
        }
        if (scope.navigation || openedFilteringBridge) {
            navigationState.value = current?.let(::projectionOf)
                ?: TaffyNavigationProjection.nothingShown()
        }
        if (scope.filtering || openedFilteringBridge) {
            filteringProjection.refreshSettings()
        }
    }

    private fun projectionOf(tab: Tab): NavigationState = TaffyNavigationProjection.of(
        tab.committedSpec,
        tab.title,
        tab.canGoBack(),
        tab.canGoForward(),
        tab.isLoading,
        errorCodeOf(tab),
        filteringProjection.isActiveFor(tab),
        filteringProjection.isExceptedFor(tab),
        filteringProjection.blockedCountFor(tab),
    )

    /**
     * The failure this tab is still about, even after the engine replaced
     * the navigation with its own error document.
     *
     * A remembered code wins. When the tab is showing that document and we
     * never heard a code — the start of the error page cleared nothing, but
     * no [onPageLoadFailed] arrived either — [NetError.ERR_FAILED] is the
     * generic "could not be reached" the mapping already has words for, so
     * Chromium's unbranded page is never what a person reads.
     */
    private fun errorCodeOf(tab: Tab): Int {
        lastError[tab.id]?.let { return it }
        if (tab.isShowingErrorPage) return NetError.ERR_FAILED
        return TaffyNavigationProjection.NO_ERROR
    }

    /** Drops a closed tab's remembered error, then rebuilds. */
    private fun forget(tab: Tab) {
        lastError.remove(tab.id)
        projectionInvalidator.tabsAndNavigationChanged()
    }

    private fun invalidateTab(tab: Tab) {
        tabProjectionCache.invalidate(tab.id)
        if (selector.currentTab === tab) {
            projectionInvalidator.tabsAndNavigationChanged()
        } else {
            projectionInvalidator.tabsChanged()
        }
    }

    private fun invalidateNavigation(tab: Tab) {
        if (selector.currentTab === tab) projectionInvalidator.navigationChanged()
    }

    private fun findTab(id: TaffyTabId): Tab? =
        id.value.toIntOrNull()?.let { selector.getTabById(it) }

    private fun currentTabIds(): Set<Int> =
        selector.models.flatMap { model ->
            (0 until model.count).mapNotNull { index -> model.getTabAt(index)?.id }
        }.toSet()

    /**
     * The address a new tab opens on.
     *
     * A blank host is a new tab with nowhere to go, which is the start page. Anything else is put
     * through the same fixup a typed address gets, so `openTab("example.test")` and typing
     * `example.test` reach the same URL rather than differing by a scheme.
     */
    /**
     * The address a new tab opens on.
     *
     * An engine page is refused here for the same reason [navigateTo] refuses one, and this is
     * the other place a string becomes a load: a new tab opens on the start page instead of on
     * `chrome://settings`. Nothing calls this with one — every caller passes a host from a page
     * that was visited — so, again, the refusal is for the caller that does not exist yet
     * (decision 0154).
     */
    private fun addressFor(host: String): String {
        if (host.isBlank()) return startPageUrl
        val url = UrlFormatter.fixupUrl(host)
        if (!url.isValid) return startPageUrl
        val spec = url.validSpecOrEmpty
        return if (TaffyNavigationProjection.isEngineInternalPage(spec)) startPageUrl else spec
    }

    /**
     * Every command's hop to the main thread, and the one check every command shares.
     *
     * A destroyed mediator refuses loudly rather than doing nothing. Everything that may call it
     * is built from the same window graph and closed with that window before [destroy] runs —
     * the screens' view models included, because their store is the window's
     * (`TaffyShellStoreOwner`) and not the activity's. A call that still arrives here is a caller
     * that outlived its window, and a quiet no-op would leave a screen driving a browser that is
     * gone; that is how a light/dark switch used to reach this line.
     */
    private suspend fun <T> onUiThread(block: suspend () -> T): T =
        withContext(Dispatchers.Main.immediate) {
            check(!destroyed) { "A destroyed browser mediator cannot act" }
            block()
        }

    companion object {
        /**
         * The identifier of the tab that was not opened.
         *
         * `openTab` has to return one. Upstream's creator answers null when a tab cannot be made
         * in this window — a profile restriction, or a model that is not there yet — and this is
         * how that is said in the seam's own vocabulary: an id that matches nothing in [tabs].
         * `DisconnectedBrowserMediator` uses the same value for the same reason.
         */
        private val NO_TAB = TaffyTabId("")

    }
}
