// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.browser.ErrandPage
import com.taffygo.browser.ui.core.browser.ErrandPagePort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import org.chromium.base.ThreadUtils
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.tab.EmptyTabObserver
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tab.TabSelectionType
import org.chromium.chrome.browser.tabmodel.TabClosureParams
import org.chromium.chrome.browser.tabmodel.TabCreatorManager
import org.chromium.chrome.browser.tabmodel.TabModel
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelUtils
import org.chromium.taffy.browser.TaffyErrandPageBridge
import org.chromium.url.GURL

/**
 * The one page an errand runs on: opened, drawn, walked and closed here.
 *
 * ## It is an ordinary tab, and that is the decision rather than the shortcut
 *
 * The obvious design is a tab that is never published to a
 * [org.chromium.chrome.browser.tabmodel.TabModel] at all — nothing to hide from
 * the switcher, nothing to leave behind in the session file, an absence rather
 * than a filter. It does not survive contact with two facts.
 *
 * **Every address that is not a web address would be silently dropped.** A
 * vendor's own app scheme, `mailto:`, `tel:` — the engine hands each of these
 * to Android as an intent, and the only path that emits one is
 * [TaffyManualNavigationCoordinator.openExternalApp], whose first term is
 * `isCurrent(sourceTab)` — `selector.getCurrentTab() == tab`. A tab in no
 * model is never the current tab, so the hand-off would not happen and nothing
 * would say why.
 *
 * The case this paragraph used to name was sharper, and is recorded because it
 * is gone rather than because it was wrong. GitHub and Facebook returned
 * through `com.taffygo.browser://auth`, so a dropped hand-off was a sign-in
 * that started and could never come back. Decision 0200 removed the account
 * plane's servers, its Android ingress left with them, and Chromium patch 0022
 * no longer registers the filter, so that URI is now emitted by nothing and
 * received by nothing. The provider flows never cared: decision 0095
 * intercepts their redirect with a navigation throttle instead.
 *
 * **A tab starts hidden.** `TabImpl` initialises itself hidden and is uncovered
 * only by `show()`, which the model calls on selection. A model-less tab would
 * need that call and its `hide()` partner from somewhere, and nothing else
 * would ever make them.
 *
 * So the errand page is a regular tab, published and selected, and the page
 * host draws it for the same reason it draws any selected tab — which is why
 * this class touches neither `TaffyBrowserActivity` nor `TaffyPageHostView`.
 *
 * ## What is paid for that, and where it is paid
 *
 * The tab is in the model, so it is in `TabPersistentStore`'s session file and
 * it is in every list built from the model. The first is bounded: the errand is
 * closed when screen SCR-110 leaves, so only a process killed mid-errand leaves
 * one behind, and what comes back is an ordinary visible tab a person can close
 * rather than a hidden one nothing can reach. The second is this class's
 * [isErrandTab], read by the projection that builds the tab list, so the
 * switcher and the counts never show it.
 *
 * ## One at a time
 *
 * A second [open] closes the first: a page with no route pointing at it is a
 * page nothing on screen can reach.
 */
internal class ChromiumErrandPageController(
    private val selector: TabModelSelector,
    private val tabCreators: TabCreatorManager,
    private val filtering: (Tab) -> Triple<Boolean, Boolean, Int>,
    private val changed: () -> Unit,
    private val show: (String) -> Boolean,
) : ErrandPagePort, Destroyable {

    private val state = MutableStateFlow<ErrandPage?>(null)
    override val page: StateFlow<ErrandPage?> = state.asStateFlow()

    private var tab: Tab? = null
    private var errandId: String = ""
    private var returnToTabId: Int = INVALID_TAB_ID
    private var minted: Long = 0
    private var destroyed = false

    /**
     * Everything an errand page reports, folded into one publication.
     *
     * The engine reports one navigation as a burst — url, title, load start,
     * load stop, security — and every one of them lands on the same projection.
     * A `MutableStateFlow` drops a write equal to what it holds, so the burst
     * costs one emission rather than five.
     */
    private val observer = object : EmptyTabObserver() {
        override fun onUrlUpdated(observed: Tab) = publish()
        override fun onTitleUpdated(observed: Tab) = publish()
        override fun onLoadStarted(observed: Tab, toDifferentDocument: Boolean) = publish()
        override fun onLoadStopped(observed: Tab, toDifferentDocument: Boolean) = publish()
        override fun onPageLoadFinished(observed: Tab, url: GURL) = publish()
        override fun onPageLoadFailed(observed: Tab, errorCode: Int) = publish()
        override fun onSSLStateUpdated(observed: Tab) = publish()
        override fun onCrash(observed: Tab) = publish()

        /**
         * The tab went away underneath the errand — closed by something else,
         * or taken with the model. The state goes to null, which is screen
         * SCR-110's signal to leave rather than draw an empty host.
         */
        override fun onDestroyed(observed: Tab) {
            if (observed !== tab) return
            tab = null
            errandId = ""
            state.value = null
            changed()
        }
    }

    /** Whether this tab is the errand page, and so belongs in no list of the person's tabs. */
    fun isErrandTab(candidate: Tab): Boolean {
        val current = tab ?: return false
        return current === candidate
    }

    override suspend fun open(url: String): String? = onUiThread {
        val creator = tabCreators.getTabCreator(/* incognito= */ false) as? TaffyTabCreator
            ?: return@onUiThread null
        val comingFrom = selector.currentTab?.id ?: INVALID_TAB_ID
        closeCurrent()
        minted += 1
        val claimed = "errand-$minted"
        // The claim runs before the tab is published and before its first
        // navigation is asked for, so no observer and no projection ever sees
        // it as one of the person's own tabs — and the visit store never sees
        // the authorization address at all. Refusing the claim destroys the
        // tab, which is the answer we want when the second of those cannot be
        // arranged: an address carrying `state`, a PKCE challenge and often the
        // account being signed in as is not worth a surface.
        val opened = creator.createErrandTab(url) { candidate ->
            if (!TaffyErrandPageBridge.keepOutOfHistory(candidate)) {
                false
            } else {
                tab = candidate
                errandId = claimed
                true
            }
        }
        if (opened == null) {
            tab = null
            errandId = ""
            return@onUiThread null
        }
        returnToTabId = comingFrom
        opened.addObserver(observer)
        publish()
        changed()
        // Opening an errand and showing it are one act. A page opened where
        // nobody can see it is worse than one that was not opened: the sign-in
        // broker would be told a surface is up, and would then wait out its
        // ten-minute deadline for a person who was never shown anything. So a
        // stack that could not move takes the page down again and this answers
        // null, which is the answer that makes the broker fall back.
        if (!show(claimed)) {
            closeCurrent()
            return@onUiThread null
        }
        claimed
    }

    override fun goBack(errandId: String): Boolean {
        ThreadUtils.assertOnUiThread()
        if (destroyed) return false
        val current = tab
        if (current == null || this.errandId != errandId || current.isDestroyed) return false
        if (!current.canGoBack()) return false
        current.goBack()
        publish()
        return true
    }

    override fun close(errandId: String) {
        ThreadUtils.assertOnUiThread()
        if (destroyed) return
        // Named rather than current, so a screen that was already replaced
        // cannot close the errand that replaced it on its way out.
        if (this.errandId == errandId) closeCurrent()
    }

    /** The window is going away; nothing is left running behind it. */
    override fun destroy() {
        if (destroyed) return
        ThreadUtils.assertOnUiThread()
        destroyed = true
        closeCurrent()
    }

    private fun closeCurrent() {
        ThreadUtils.assertOnUiThread()
        val current = tab ?: return
        val returnTo = returnToTabId
        tab = null
        errandId = ""
        returnToTabId = INVALID_TAB_ID
        state.value = null
        current.removeObserver(observer)
        if (!current.isDestroyed) {
            val model = selector.getModel(/* incognito= */ false)
            model.tabRemover.closeTabs(
                TabClosureParams.closeTab(current).allowUndo(false).build(),
                /* allowDialog= */ false,
            )
            // The model picks a successor by its own rule, and its rule does not
            // know where the person was. Putting them back where they came from
            // is the whole of "an errand a person comes back from": the tab that
            // was selected when the errand started is the tab that should be
            // selected when it ends.
            selectAgain(model, returnTo)
        }
        // The list changed even though the model's own count did not move for
        // any surface that can see it: the errand tab was excluded while it was
        // open, so its going is a change only this class knows about.
        changed()
    }

    private fun selectAgain(model: TabModel, tabId: Int) {
        if (tabId == INVALID_TAB_ID) return
        val index = TabModelUtils.getTabIndexById(model, tabId)
        if (index == TabModel.INVALID_TAB_INDEX) return
        model.setIndex(index, TabSelectionType.FROM_USER)
    }

    private fun publish() {
        val current = tab ?: return
        if (current.isDestroyed) return
        // Active, excepted, blocked — the same three facts the browsing surface
        // reads, answered by the plane this tab belongs to (decision 0128). The
        // errand toolbar cannot change any of them, but it must not misreport
        // them either: a vendor page on an allowed site is filtered no more
        // than that site is anywhere else.
        val (active, excepted, blocked) = filtering(current)
        state.value = ErrandPage(
            errandId = errandId,
            navigation = TaffyNavigationProjection.of(
                current.committedSpec,
                current.title.orEmpty(),
                current.canGoBack(),
                /* canGoForward= */ false,
                current.isLoading,
                TaffyNavigationProjection.NO_ERROR,
                active,
                excepted,
                blocked,
            ),
        )
    }

    private suspend fun <T> onUiThread(block: () -> T): T =
        withContext(Dispatchers.Main.immediate) {
            check(!destroyed) { "A destroyed errand page controller cannot act" }
            block()
        }

    private companion object {
        /** No tab to come back to: the errand was started with nothing selected. */
        const val INVALID_TAB_ID = -1
    }
}
