// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.TaffyTabComponent
import com.taffygo.browser.ui.app.TaffyTabComponentParent
import com.taffygo.browser.ui.core.common.di.TaffyTabIdentity
import com.taffygo.browser.ui.core.page.PageIntelligenceClient
import java.io.Closeable
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.EmptyTabObserver
import org.chromium.chrome.browser.tab.Tab
import org.chromium.content_public.browser.WebContents
import org.chromium.ui.base.WindowAndroid

/** Profile-owned observer that keeps one Tab component aligned with the live WebContents. */
internal class TaffyTabComponentOwner(
    private val profile: Profile,
    private val tab: Tab,
    private val profileComponent: TaffyTabComponentParent,
    private val pageFactory: TaffyPageIntelligenceFactory,
    private val onDestroyed: (TaffyTabComponentOwner) -> Unit,
) : EmptyTabObserver(),
    Closeable {
    private var component: TaffyTabComponent? = null
    private var webContents: WebContents? = null
    private var contentGeneration = 0uL
    private var closed = false

    init {
        check(tab.profile === profile) { "A Tab owner cannot cross its Chromium profile" }
        check(!tab.isDestroyed) { "A destroyed Tab cannot acquire a component" }
        tab.addObserver(this)
        replaceContent(tab.webContents)
    }

    fun pageIntelligence(): PageIntelligenceClient? = component?.pageIntelligence()

    override fun onContentChanged(tab: Tab) {
        replaceContent(tab.webContents)
    }

    override fun onDestroyed(tab: Tab) {
        close()
        onDestroyed(this)
    }

    override fun onActivityAttachmentChanged(tab: Tab, window: WindowAndroid?) = Unit

    override fun close() {
        if (closed) return
        closed = true
        tab.removeObserver(this)
        closeComponent()
    }

    private fun replaceContent(next: WebContents?) {
        if (closed || next === webContents) return
        closeComponent()
        if (next == null || next.isDestroyed) return

        contentGeneration += 1uL
        val slot = TaffyTabPageClientSlot()
        val built = profileComponent.tabBuilder()
            .identity(TaffyTabIdentity("tab:${tab.id}:content:$contentGeneration"))
            .pageIntelligence(slot)
            .build()
        var owned: TaffyOwnedPageIntelligenceClient? = null
        try {
            val created = pageFactory.create(profile, tab, next, built.lifetime().scope)
            owned = created
            slot.bind(created.client)
            built.lifetime().own(
                Closeable {
                    slot.close()
                    created.transport.close()
                },
            )
        } catch (failure: Throwable) {
            slot.close()
            val cleanup = listOfNotNull(owned?.transport, built.lifetime())
            try {
                closeAllOwnedResources(cleanup)
            } catch (closeFailure: Throwable) {
                failure.addSuppressed(closeFailure)
            }
            throw failure
        }
        webContents = next
        component = built
    }

    private fun closeComponent() {
        val closing = component
        component = null
        webContents = null
        closing?.lifetime()?.close()
    }
}
