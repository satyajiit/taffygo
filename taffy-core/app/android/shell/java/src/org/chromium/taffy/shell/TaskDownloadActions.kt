// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.net.Uri
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withTimeoutOrNull
import org.chromium.base.lifetime.Destroyable
import org.chromium.components.offline_items_collection.ContentId
import org.chromium.components.offline_items_collection.LegacyHelpers
import org.chromium.components.offline_items_collection.ShareCallback

/** Main-thread file controls over the current profile provider and native task attribution. */
class TaskDownloadActions(
    private val current: () -> List<DownloadRecord>,
    private val canOpen: (taskId: String, downloadGuid: String) -> Boolean,
    private val perform: (DownloadId, DownloadAction) -> Boolean,
    private val requestFile: (ContentId, ShareCallback) -> Unit,
    private val openPdf: (fileName: String, uri: Uri) -> Boolean,
    private val timeoutMillis: Long = 10_000L,
) : Destroyable {
    private var closed = false
    private var pending: CompletableDeferred<Uri?>? = null

    fun completedForTask(taskId: String): List<DownloadRecord> =
        if (closed || taskId.isBlank()) emptyList() else current().filter { eligible(taskId, it) }

    suspend fun openForTask(taskId: String, id: DownloadId): Boolean {
        currentCoroutineContext().ensureActive()
        pending?.complete(null)
        pending = null
        if (closed || taskId.isBlank()) return false
        val record = current().firstOrNull { it.id == id } ?: return false
        if (!eligible(taskId, record)) return false
        if (!record.mimeType.equals("application/pdf", ignoreCase = true)) {
            return perform(id, DownloadAction.OPEN)
        }

        // Upstream openItem sends supported Android PDFs to Chrome's activity.
        // A task PDF instead gets a URI from this profile's native provider and
        // is handed to the person's viewer after a second authorization check.
        val answer = CompletableDeferred<Uri?>()
        pending = answer
        val requestedGuid = guid(record)
        val namespace = LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE
        return try {
            requestFile(ContentId(namespace, requestedGuid)) { returned, info ->
                val exact = returned != null && returned.namespace == namespace &&
                    returned.id == requestedGuid
                answer.complete(info?.uri.takeIf { exact && pending === answer && !closed })
            }
            val uri = withTimeoutOrNull(timeoutMillis) { answer.await() } ?: return false
            currentCoroutineContext().ensureActive()
            val latest = current().firstOrNull { it.id == id } ?: return false
            if (closed || pending !== answer || latest != record ||
                !eligible(taskId, latest) || uri.scheme != "content" ||
                uri.authority.isNullOrEmpty() || uri.path.isNullOrEmpty()
            ) return false
            openPdf(latest.fileName, uri)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: RuntimeException) {
            return false
        } finally {
            if (pending === answer) pending = null
            answer.cancel()
        }
    }

    override fun destroy() {
        closed = true
        pending?.complete(null)
        pending = null
    }

    private fun eligible(taskId: String, record: DownloadRecord): Boolean {
        if (record.state != DownloadState.COMPLETE ||
            DownloadAction.OPEN !in record.allowedActions
        ) return false
        // The provider token binds a namespace and an id. Only Chromium's
        // native download namespace speaks the GUID the ownership registry
        // recorded; an Android-system or other provider id is never guessed.
        val prefix = namespacePrefix()
        if (!record.id.value.startsWith(prefix)) return false
        val guid = guid(record)
        return guid.isNotEmpty() && canOpen(taskId, guid)
    }

    private fun guid(record: DownloadRecord): String = record.id.value.removePrefix(namespacePrefix())

    private fun namespacePrefix(): String = LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE.let {
        "${it.length}:$it"
    }
}
