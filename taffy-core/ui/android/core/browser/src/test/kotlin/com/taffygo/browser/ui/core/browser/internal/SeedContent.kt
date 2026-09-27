// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

/**
 * What the UI host starts with.
 *
 * Every host here is a `.test` name from the deterministic fixture corpus, so
 * nothing in the UI host ever points at a live site, and every value is fixed
 * so a screenshot taken twice is the same screenshot.
 */
internal object SeedContent {

    /** The fixture hosts the UI host knows about. */
    const val DOCS_HOST = "docs.example.test"
    const val SHOP_HOST = "shop.example.test"
    const val REVIEWS_HOST = "reviews.example.test"

    /** A host with nothing behind it, so the error page has a way in. */
    const val UNREACHABLE_HOST = "gone.example.test"

    /** A host that needs a sign-in, so waiting for you has a way in. */
    const val SIGN_IN_HOST = "account.example.test"

    val tabs: List<Tab> = listOf(
        Tab(TabId("tab_1"), titleFor(DOCS_HOST), DOCS_HOST, isSelected = true),
        Tab(TabId("tab_2"), titleFor(SHOP_HOST), SHOP_HOST),
        Tab(TabId("tab_3"), titleFor(REVIEWS_HOST), REVIEWS_HOST),
    )

    val navigation = com.taffygo.browser.ui.core.browser.NavigationState(
        host = DOCS_HOST,
        title = titleFor(DOCS_HOST),
    )

    val downloads: List<DownloadRecord> = listOf(
        DownloadRecord(
            id = DownloadId("dl_1"),
            fileName = "retention-policy.pdf",
            host = DOCS_HOST,
            totalBytes = 2_400_000,
            downloadedBytes = 2_400_000,
            state = DownloadState.COMPLETE,
            allowedActions = setOf(DownloadAction.OPEN, DownloadAction.SHARE, DownloadAction.REMOVE),
        ),
        DownloadRecord(
            id = DownloadId("dl_2"),
            fileName = "price-list.csv",
            host = SHOP_HOST,
            totalBytes = 180_000,
            downloadedBytes = 96_000,
            state = DownloadState.RUNNING,
            allowedActions = setOf(DownloadAction.PAUSE, DownloadAction.CANCEL),
        ),
        DownloadRecord(
            id = DownloadId("dl_3"),
            fileName = "comparison.md",
            host = REVIEWS_HOST,
            totalBytes = null,
            downloadedBytes = 0,
            state = DownloadState.FAILED,
            allowedActions = setOf(DownloadAction.REMOVE),
        ),
    )

    /** A tab to open when the last one closes. */
    fun newTab(nowEpochMillis: Long) = Tab(
        id = TabId("tab_$nowEpochMillis"),
        title = titleFor(DOCS_HOST),
        host = DOCS_HOST,
        isSelected = true,
    )

    /** The title a fixture host serves. */
    fun titleFor(host: String): String = when (host) {
        DOCS_HOST -> "Retention policy"
        SHOP_HOST -> "Product listing"
        REVIEWS_HOST -> "Independent review"
        SIGN_IN_HOST -> "Sign in"
        else -> host
    }

    /** Why a host does not load, when it does not. */
    fun failureFor(host: String): PageLoadFailure? =
        if (host == UNREACHABLE_HOST) UNREACHABLE_HOST_FAILURE else null
}
