// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.page.PageIntelligenceClient
import com.taffygo.browser.ui.core.page.SelectedPageIntelligenceClient
import java.io.Closeable
import org.chromium.base.Callback
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.taffy.browser.TaffyTaskSourceSelectionBridge
import taffy.core_api.CoreApiSubmissionStatus
import taffy.core_api.PageInspectorAvailability
import taffy.core_api.PageInspectorDocumentsView
import taffy.core_api.PageInspectorSnapshotResult
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.PageSnapshotExportResult

/** Window-owned route to the selected Tab component's bounded page projection. */
internal class TaffySelectedTabRouter(
    private val selector: TabModelSelector,
    private val provider: ChromiumTaffyProfileRuntimeProvider,
    private val taskSources: TaffyTaskSourceSelectionBridge,
) : SelectedPageIntelligenceClient,
    Closeable {
    private var closed = false
    private var routedTab: Tab? = selector.currentTab
    private val exportRouter = SelectedPageExportRouter(::selectedClient)
    private val selectionObserver = Callback<Tab?> { tab ->
        if (!closed) {
            if (tab !== routedTab) {
                routedTab = tab
                exportRouter.selectionChanged()
            }
            check(taskSources.select(TaskSourceTabs.selection(tab))) {
                "The selected tab could not be published"
            }
        }
    }

    init {
        selector.currentTabSupplier.addSyncObserver(selectionObserver)
        try {
            check(taskSources.select(TaskSourceTabs.selection(selector.currentTab))) {
                "The initial selected tab could not be published"
            }
        } catch (failure: Throwable) {
            selector.currentTabSupplier.removeObserver(selectionObserver)
            throw failure
        }
    }

    override val isAvailable: Boolean
        get() = selectedClient()?.isAvailable == true

    override suspend fun documents(): PageInspectorDocumentsView =
        selectedClient()?.documents()
            ?: PageInspectorDocumentsView(PageInspectorAvailability.NO_SELECTED_PAGE, emptyList())

    override suspend fun snapshot(documentId: String): PageInspectorSnapshotResult =
        selectedClient()?.snapshot(documentId)
            ?: PageInspectorSnapshotResult(PageInspectorAvailability.NO_SELECTED_PAGE, null)

    override suspend fun exportSnapshot(
        requestId: String,
        documentId: String,
        format: PageSnapshotExportFormat,
    ): PageSnapshotExportResult =
        exportRouter.exportSnapshot(requestId, documentId, format)

    override suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus =
        exportRouter.cancelExport(requestId)

    override fun close() {
        if (closed) return
        closed = true
        exportRouter.close()
        var failure: Throwable? = null
        try {
            selector.currentTabSupplier.removeObserver(selectionObserver)
        } catch (closeFailure: Throwable) {
            failure = closeFailure
        }
        try {
            check(taskSources.select(null)) { "The selected task source could not be cleared" }
        } catch (closeFailure: Throwable) {
            failure?.addSuppressed(closeFailure) ?: run { failure = closeFailure }
        }
        failure?.let { throw it }
    }

    private fun selectedClient(): PageIntelligenceClient? {
        if (closed) return null
        val tab: Tab = selector.currentTab ?: return null
        if (tab.isDestroyed || tab.webContents == null) return null
        return provider.runtime(tab.profile).requireTab(tab).pageIntelligence()
    }
}
