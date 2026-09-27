// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.pageinspector

import android.os.Handler
import android.os.Looper
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.page.PageIntelligenceClient
import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.suspendCancellableCoroutine
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.Tab
import org.chromium.content_public.browser.WebContents
import org.chromium.taffy.browser.TaffyPageInspectorBridge
import org.chromium.taffy.core_api.mojom.TaffyPageInspector
import taffy.core_api.CoreApiSubmissionStatus
import taffy.core_api.MAX_IDENTIFIER_BYTES
import taffy.core_api.MAX_PAGE_SNAPSHOT_EXPORT_BYTES
import taffy.core_api.MAX_SITE_SKILLS
import taffy.core_api.MAX_SKILL_ID_BYTES
import taffy.core_api.MAX_SKILL_STEPS
import taffy.core_api.PageInspectorAdapterKind
import taffy.core_api.PageInspectorAdapterStatus
import taffy.core_api.PageInspectorAdapterView
import taffy.core_api.PageInspectorAvailability
import taffy.core_api.PageInspectorBudgetKind
import taffy.core_api.PageInspectorClaim
import taffy.core_api.PageInspectorDocumentKind
import taffy.core_api.PageInspectorDocumentState
import taffy.core_api.PageInspectorDocumentView
import taffy.core_api.PageInspectorDocumentsView
import taffy.core_api.PageInspectorEdgeView
import taffy.core_api.PageInspectorFrameView
import taffy.core_api.PageInspectorNodeRole
import taffy.core_api.PageInspectorNodeView
import taffy.core_api.PageInspectorRedactionView
import taffy.core_api.PageInspectorRelationship
import taffy.core_api.PageInspectorSensitivity
import taffy.core_api.PageInspectorSnapshotResult
import taffy.core_api.PageInspectorSnapshotView
import taffy.core_api.PageInspectorTruncationView
import taffy.core_api.PageInspectorWarningCode
import taffy.core_api.PageSnapshotExportAvailability
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.PageSnapshotExportResult
import taffy.core_api.PageSnapshotExportView
import taffy.core_api.SiteSkillOfferAvailability
import taffy.core_api.SiteSkillOfferView

/** One generated-Mojo page client bound to one exact WebContents lifetime. */
class ChromiumPageIntelligenceClient private constructor(
    private val proxy: TaffyPageInspector,
    private val scope: CoroutineScope,
) : PageIntelligenceClient, Closeable {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val closed = AtomicBoolean(false)

    override val isAvailable: Boolean
        get() = !closed.get() && scope.coroutineContext[kotlinx.coroutines.Job]?.isActive != false

    override suspend fun documents(): PageInspectorDocumentsView =
        await(PageInspectorDocumentsView(PageInspectorAvailability.CORE_UNAVAILABLE, emptyList())) {
            callback -> proxy.getDocuments { callback(it.toCoreDocuments()) }
        }

    override suspend fun snapshot(documentId: String): PageInspectorSnapshotResult {
        if (documentId != DOCUMENT_ID) return invalidSnapshot()
        return await(invalidSnapshot()) { callback ->
            proxy.getSnapshot(documentId) { callback(it.toCoreSnapshotResult()) }
        }
    }

    override suspend fun exportSnapshot(
        requestId: String,
        documentId: String,
        format: PageSnapshotExportFormat,
    ): PageSnapshotExportResult {
        if (requestId.isBlank() || documentId != DOCUMENT_ID) return invalidExport()
        return await(
            unavailable = invalidExport(),
            onCancel = { proxy.cancelExport(requestId) {} },
        ) { callback ->
            proxy.exportSnapshot(requestId, documentId, format.wire.toInt()) {
                callback(it.toCoreExportResult(requestId, documentId, format))
            }
        }
    }

    override suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus =
        if (requestId.isBlank()) {
            CoreApiSubmissionStatus.INVALID_REQUEST
        } else {
            await(CoreApiSubmissionStatus.CORE_UNAVAILABLE) { callback ->
                proxy.cancelExport(requestId) { status ->
                    callback(
                        CoreApiSubmissionStatus.fromWire(status.toUInt())
                            ?: CoreApiSubmissionStatus.INVALID_REQUEST,
                    )
                }
            }
        }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        val closePipe = Runnable { proxy.close() }
        if (Looper.myLooper() == Looper.getMainLooper()) closePipe.run() else mainHandler.post(closePipe)
    }

    private suspend fun <T> await(
        unavailable: T,
        onCancel: (() -> Unit)? = null,
        call: (((T) -> Unit) -> Unit),
    ): T =
        suspendCancellableCoroutine { continuation ->
            continuation.invokeOnCancellation {
                if (onCancel != null) mainHandler.post {
                    if (!closed.get()) onCancel()
                }
            }
            mainHandler.post {
                if (!isAvailable) {
                    if (continuation.isActive) continuation.resume(unavailable)
                    return@post
                }
                call { value -> if (continuation.isActive) continuation.resume(value) }
            }
        }

    companion object {
        private const val DOCUMENT_ID = "selected-page"

        @JvmStatic
        fun create(
            profile: Profile,
            tab: Tab,
            webContents: WebContents,
            scope: CoroutineScope,
        ): ChromiumPageIntelligenceClient {
            require(tab.webContents === webContents) { "The page client must match the tab WebContents" }
            check(Looper.myLooper() == Looper.getMainLooper()) {
                "The page client must be created on Chromium's UI thread"
            }
            return ChromiumPageIntelligenceClient(
                TaffyPageInspectorBridge.connect(profile, webContents),
                scope,
            )
        }
    }
}

private fun invalidSnapshot() =
    PageInspectorSnapshotResult(PageInspectorAvailability.INVALID_RESPONSE, null)

private fun invalidExport() =
    PageSnapshotExportResult(PageSnapshotExportAvailability.INVALID_RESPONSE, null)

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun org.chromium.taffy.core_api.mojom.PageSnapshotExportResult?.toCoreExportResult(
    requestId: String,
    documentId: String,
    requestedFormat: PageSnapshotExportFormat,
): PageSnapshotExportResult {
    if (this == null) return invalidExport()
    val availability = PageSnapshotExportAvailability.fromWire(availability.toUInt())
        ?: return invalidExport()
    val projected = snapshotExport?.toCoreExport() ?: if (snapshotExport == null) null else {
        return invalidExport()
    }
    if ((availability == PageSnapshotExportAvailability.AVAILABLE) != (projected != null)) {
        return invalidExport()
    }
    if (projected != null && (projected.request_id != requestId ||
            projected.document_id != documentId || projected.format != requestedFormat)
    ) {
        return invalidExport()
    }
    return PageSnapshotExportResult(availability, projected)
}

private fun org.chromium.taffy.core_api.mojom.PageSnapshotExportView.toCoreExport():
    PageSnapshotExportView? {
    val projectedFormat = PageSnapshotExportFormat.fromWire(format.toUInt()) ?: return null
    val expectedType = when (projectedFormat) {
        PageSnapshotExportFormat.MARKDOWN -> "text/markdown"
        PageSnapshotExportFormat.CANONICAL_JSON -> "application/json"
    }
    val expectedName = when (projectedFormat) {
        PageSnapshotExportFormat.MARKDOWN -> "taffy-page-snapshot.md"
        PageSnapshotExportFormat.CANONICAL_JSON -> "taffy-page-snapshot.json"
    }
    if (requestId.isNullOrEmpty() || documentId.isNullOrEmpty() || origin.isNullOrEmpty() ||
        mimeType.isNullOrEmpty() || suggestedFileName.isNullOrEmpty() || content == null ||
        documentRevision <= 0L || mimeType != expectedType || suggestedFileName != expectedName ||
        content.isEmpty() || content.size > MAX_PAGE_SNAPSHOT_EXPORT_BYTES ||
        nodeCount < 0 || redactedFieldCount < 0 || suppressedSecretValueCount < 0 ||
        withheldFieldCount < 0 || capturedAtEpochMs <= 0L
    ) return null
    return PageSnapshotExportView(
        requestId,
        documentId,
        documentRevision.toULong(),
        origin,
        projectedFormat,
        mimeType,
        suggestedFileName,
        content.copyOf(),
        nodeCount.toUInt(),
        redactedFieldCount.toUInt(),
        suppressedSecretValueCount.toUInt(),
        withheldFieldCount.toUInt(),
        capturedAtEpochMs.toULong(),
        sourceQueryWithheld,
        sourceFragmentWithheld,
        secureContext,
    )
}

private fun org.chromium.taffy.core_api.mojom.PageInspectorDocumentsView?.toCoreDocuments():
    PageInspectorDocumentsView {
    if (this == null) {
        return PageInspectorDocumentsView(PageInspectorAvailability.INVALID_RESPONSE, emptyList())
    }
    val availability = PageInspectorAvailability.fromWire(availability.toUInt())
        ?: return PageInspectorDocumentsView(PageInspectorAvailability.INVALID_RESPONSE, emptyList())
    val projected = documents?.mapNotNull { document ->
        val kind = PageInspectorDocumentKind.fromWire(document.kind.toUInt()) ?: return@mapNotNull null
        val claims = document.claims
            ?.map { PageInspectorClaim.fromWire(it.toUInt()) }
            ?.filterNotNull()
            ?: emptyList()
        if (document.documentId.isNullOrEmpty() || document.host == null ||
            claims.size != (document.claims?.size ?: 0)
        ) return@mapNotNull null
        PageInspectorDocumentView(document.documentId, kind, document.host, claims)
    } ?: emptyList()
    return if ((availability == PageInspectorAvailability.AVAILABLE) != projected.isNotEmpty() ||
        projected.size != (documents?.size ?: 0)
    ) {
        PageInspectorDocumentsView(PageInspectorAvailability.INVALID_RESPONSE, emptyList())
    } else {
        PageInspectorDocumentsView(availability, projected)
    }
}

private fun org.chromium.taffy.core_api.mojom.PageInspectorSnapshotResult?.toCoreSnapshotResult():
    PageInspectorSnapshotResult {
    if (this == null) return invalidSnapshot()
    val availability = PageInspectorAvailability.fromWire(availability.toUInt()) ?: return invalidSnapshot()
    val projected = snapshot?.toCoreSnapshot() ?: if (snapshot == null) null else return invalidSnapshot()
    return if ((availability == PageInspectorAvailability.AVAILABLE) != (projected != null)) {
        invalidSnapshot()
    } else {
        PageInspectorSnapshotResult(availability, projected)
    }
}

private fun org.chromium.taffy.core_api.mojom.PageInspectorSnapshotView.toCoreSnapshot():
    PageInspectorSnapshotView? {
    val state = PageInspectorDocumentState.fromWire(documentState.toUInt()) ?: return null
    val projectedAdapters = adapters?.mapNotNull { adapter ->
        val kind = PageInspectorAdapterKind.fromWire(adapter.kind.toUInt()) ?: return@mapNotNull null
        val status = PageInspectorAdapterStatus.fromWire(adapter.status.toUInt()) ?: return@mapNotNull null
        PageInspectorAdapterView(kind, status, adapter.version.toUInt())
    } ?: emptyList()
    val projectedNodes = nodes?.mapNotNull { it.toCoreNode() } ?: emptyList()
    val projectedEdges = edges?.mapNotNull { edge ->
        val relationship = PageInspectorRelationship.fromWire(edge.relationship.toUInt())
            ?: return@mapNotNull null
        PageInspectorEdgeView(edge.fromDisplayId, edge.toDisplayId, relationship, edge.inferred)
    } ?: emptyList()
    val projectedFrames = frames?.map { frame ->
        PageInspectorFrameView(frame.mainFrame, frame.outOfProcess, frame.crossOrigin, frame.included)
    } ?: emptyList()
    val truncation = truncation?.toCoreTruncation() ?: return null
    val redaction = redaction?.let {
        PageInspectorRedactionView(
            it.redactedFieldCount.toUInt(),
            it.suppressedSecretCount.toUInt(),
            it.sensitiveZoneCount.toUInt(),
            it.filteredFrameCount.toUInt(),
        )
    } ?: return null
    val projectedWarnings = warnings
        ?.map { PageInspectorWarningCode.fromWire(it.toUInt()) }
        ?.filterNotNull()
        ?: emptyList()
    val offerAvailability = SiteSkillOfferAvailability.fromWire(
        siteSkillOfferAvailability.toUInt(),
    ) ?: return null
    val projectedOffers = siteSkillOffers?.mapNotNull { offer ->
        if (offer.offerId.isBlank() || offer.offerId.length > MAX_IDENTIFIER_BYTES ||
            offer.skillId.isBlank() || offer.skillId.length > MAX_SKILL_ID_BYTES ||
            offer.activeVersion <= 0 || offer.stepCount <= 0 ||
            offer.stepCount > MAX_SKILL_STEPS
        ) return@mapNotNull null
        SiteSkillOfferView(
            offer.offerId,
            offer.skillId,
            offer.activeVersion.toUInt(),
            offer.stepCount.toUInt(),
        )
    } ?: emptyList()
    if (projectedAdapters.size != (adapters?.size ?: 0) ||
        projectedNodes.size != (nodes?.size ?: 0) ||
        projectedEdges.size != (edges?.size ?: 0) ||
        projectedWarnings.size != (warnings?.size ?: 0) ||
        projectedOffers.size != (siteSkillOffers?.size ?: 0) ||
        projectedOffers.size > MAX_SITE_SKILLS ||
        (offerAvailability != SiteSkillOfferAvailability.AVAILABLE &&
            projectedOffers.isNotEmpty())
    ) return null
    return PageInspectorSnapshotView(
        documentId,
        documentRevision.toULong(),
        host,
        secureContext,
        privateProfile,
        state,
        projectedAdapters,
        projectedNodes,
        projectedEdges,
        projectedFrames,
        truncation,
        redaction,
        projectedWarnings,
        offerAvailability,
        projectedOffers,
    )
}

private fun org.chromium.taffy.core_api.mojom.PageInspectorNodeView.toCoreNode():
    PageInspectorNodeView? {
    val role = PageInspectorNodeRole.fromWire(role.toUInt()) ?: return null
    val sensitivity = PageInspectorSensitivity.fromWire(sensitivity.toUInt()) ?: return null
    return PageInspectorNodeView(
        displayId,
        role,
        name,
        sensitivity,
        textRunCount.toUInt(),
        textByteCount.toULong(),
        valuePresent,
        valueWithheld,
    )
}

private fun org.chromium.taffy.core_api.mojom.PageInspectorTruncationView.toCoreTruncation():
    PageInspectorTruncationView? {
    val budgets = budgetsReached
        ?.map { PageInspectorBudgetKind.fromWire(it.toUInt()) }
        ?.filterNotNull()
        ?: emptyList()
    if (budgets.size != (budgetsReached?.size ?: 0)) return null
    return PageInspectorTruncationView(
        truncated,
        budgets,
        omittedNodeCount.toUInt(),
        omittedTextBytes.toUInt(),
        omittedFrameCount.toUInt(),
        mayChangeAnswer,
    )
}
