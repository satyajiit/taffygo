// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import java.io.Closeable
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.tab.EmptyTabObserver
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabModelObserver
import org.chromium.components.find_in_page.FindInPageBridge
import org.chromium.components.find_in_page.FindNotificationDetails
import org.chromium.content_public.browser.WebContents
import org.chromium.url.GURL

/** Chromium object lifetimes hidden from the find-in-page state machine. */
internal class ChromiumFindInPageEnvironment(
    private val selector: TabModelSelector,
) : ChromiumFindInPagePort.Environment {
    override fun selectedTarget(): ChromiumFindInPagePort.Target? {
        ThreadUtils.assertOnUiThread()
        val tab = selector.currentTab ?: return null
        if (tab.isOffTheRecord || tab.isDestroyed || tab.isClosing) return null
        val contents = tab.webContents ?: return null
        if (contents.isDestroyed) return null
        return ChromiumTarget(tab, contents)
    }

    override fun observeSelection(onChanged: () -> Unit): Closeable {
        val observer = object : TabModelSelectorTabModelObserver(selector) {
            override fun didSelectTab(tab: Tab, type: Int, lastId: Int) = onChanged()
        }
        return Closeable(observer::destroy)
    }

    private class ChromiumTarget(
        private val tab: Tab,
        private val contents: WebContents,
    ) : ChromiumFindInPagePort.Target {
        override val identity: Any = PageIdentity(tab, contents)

        override fun observeInvalidation(onInvalidated: () -> Unit): Closeable {
            val observer = object : EmptyTabObserver() {
                override fun onPageLoadStarted(tab: Tab, url: GURL) = onInvalidated()

                override fun onLoadStarted(tab: Tab, toDifferentDocument: Boolean) {
                    if (toDifferentDocument) onInvalidated()
                }

                override fun onContentChanged(tab: Tab) = onInvalidated()
                override fun onUrlUpdated(tab: Tab) = onInvalidated()
                override fun onCrash(tab: Tab) = onInvalidated()
                override fun onDestroyed(tab: Tab) = onInvalidated()

                override fun onClosingStateChanged(tab: Tab, closing: Boolean) {
                    if (closing) onInvalidated()
                }
            }
            tab.addObserver(observer)
            return Closeable { tab.removeObserver(observer) }
        }

        override fun openRequest(
            onResult: (ChromiumFindInPagePort.Update) -> Unit,
        ): ChromiumFindInPagePort.Request = ChromiumRequest(tab, contents, onResult)
    }

    private class ChromiumRequest(
        private val tab: Tab,
        contents: WebContents,
        private val onResult: (ChromiumFindInPagePort.Update) -> Unit,
    ) : ChromiumFindInPagePort.Request {
        private val bridge = FindInPageBridge(contents)
        private val observer = object : EmptyTabObserver() {
            override fun onFindResultAvailable(result: FindNotificationDetails) {
                onResult(
                    ChromiumFindInPagePort.Update(
                        result.activeMatchOrdinal,
                        result.numberOfMatches,
                        result.finalUpdate,
                    ),
                )
            }
        }
        private var closed = false

        init {
            try {
                tab.addObserver(observer)
            } catch (failure: RuntimeException) {
                bridge.destroy()
                throw failure
            }
        }

        override fun start(query: String, forward: Boolean) {
            check(!closed) { "A closed find request cannot be started" }
            bridge.startFinding(query, forward, /* caseSensitive= */ false)
        }

        override fun stop(clearSelection: Boolean) {
            if (!closed) bridge.stopFinding(clearSelection)
        }

        override fun close() {
            if (closed) return
            closed = true
            var failure: RuntimeException? = null
            try {
                tab.removeObserver(observer)
            } catch (caught: RuntimeException) {
                failure = caught
            }
            try {
                bridge.destroy()
            } catch (caught: RuntimeException) {
                failure?.addSuppressed(caught) ?: run { failure = caught }
            }
            failure?.let { throw it }
        }
    }

    /** Equality over the two native owners must never call their value equality. */
    private class PageIdentity(
        private val tab: Tab,
        private val contents: WebContents,
    ) {
        override fun equals(other: Any?): Boolean =
            other is PageIdentity && other.tab === tab && other.contents === contents

        override fun hashCode(): Int =
            31 * System.identityHashCode(tab) + System.identityHashCode(contents)
    }
}
