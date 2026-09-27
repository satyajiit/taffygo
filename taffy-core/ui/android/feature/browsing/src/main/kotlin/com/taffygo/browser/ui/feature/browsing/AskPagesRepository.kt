// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Open tabs Ask may point at.
 *
 * The browser list is the source when one exists. Empty and unavailable are
 * the honest answers when it does not; they never mint a page.
 */
interface AskPagesRepository {
    /** Every open tab, including ones this surface will not offer. */
    val tabs: StateFlow<List<Tab>>

    /** Local browser artwork only; missing marks never cause a network fetch. */
    val tabArtwork: StateFlow<Map<TabId, TabArtwork>> get() = NoTabArtwork
    /** Previously stored marks for eligible page hosts. */
    val siteMarks: StateFlow<Map<String, Bitmap>> get() = NoSiteMarks
    /** Reads the local favicon store for hosts already visible to the picker. */
    suspend fun requestSiteMarks(hosts: Set<String>) = Unit

    /** Whether this listing is ready, still loading, or cannot be read. */
    val status: AskPagesSnapshot.Status
        get() = AskPagesSnapshot.Status.READY

    /** No open tabs, and nothing is being withheld. */
    class Empty : AskPagesRepository {
        override val tabs: StateFlow<List<Tab>> = MutableStateFlow(emptyList())
    }

    /** The listing cannot be read on this build. */
    class Unavailable : AskPagesRepository {
        override val tabs: StateFlow<List<Tab>> = MutableStateFlow(emptyList())
        override val status: AskPagesSnapshot.Status = AskPagesSnapshot.Status.UNAVAILABLE
    }

    companion object {
        private val NoTabArtwork = MutableStateFlow<Map<TabId, TabArtwork>>(emptyMap())
        private val NoSiteMarks = MutableStateFlow<Map<String, Bitmap>>(emptyMap())
    }
}

/** Listing backed by the live browser tab list. */
internal class BrowserAskPagesRepository(
    private val browser: BrowserRepository,
) : AskPagesRepository {
    override val tabs: StateFlow<List<Tab>> = browser.tabs
    override val tabArtwork = browser.tabArtwork
    override val siteMarks = browser.siteMarks
    override suspend fun requestSiteMarks(hosts: Set<String>) = browser.requestSiteMarks(hosts)
}

/** The repository this feature uses when a browser list is in the graph. */
internal fun askPagesRepository(browser: BrowserRepository): AskPagesRepository =
    BrowserAskPagesRepository(browser)
