// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.net.Uri
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent

internal class BackupUiTestHost : BackupWindowHost {
    val sessions = mutableListOf<Key>()
    val selections = mutableListOf<Set<BackupWindowHost.ContentClass>>()
    val callbacks = mutableListOf<(BackupWindowHost.ExportPreparation) -> Unit>()
    val events = mutableListOf<AnalyticsEvent>()
    var available = true
    var documentCalls = 0
    var deletionRequest: BackupDeletionRequest? = null
    var deletionSelection: suspend (BackupDeletionRequest, Uri) -> BackupDocumentCopy? = { _, _ -> null }
    val restoreRequests = mutableListOf<Pair<BackupRecoveryKeySession, String>>()
    val restoreCallbacks = mutableListOf<(BackupRestorePreparation) -> Unit>()
    val stageReviews = mutableListOf<BackupRestoreReview>()
    val stageCallbacks = mutableListOf<(BackupRestoreStageResult) -> Unit>()
    val commitReviews = mutableListOf<BackupRestoreReview>()
    val commitCallbacks = mutableListOf<(BackupRestoreCommitResult) -> Unit>()
    val resolutionChoices = mutableListOf<Pair<BackupRestoreReview, BackupRestoreResolutionChoice>>()
    val resolutionCallbacks = mutableListOf<(BackupRestoreResolutionResult) -> Unit>()
    val discoveryCallbacks = mutableListOf<(BackupRestoreDiscoveryResult) -> Unit>()
    val reviewWithdrawals = mutableListOf<BackupRestoreReview>()
    var discoveryRequest: BackupRestoreDiscoveryRequest? = null
    var duringDiscovery: ((BackupRestoreDiscoveryResult) -> Unit) -> Unit = {}

    fun viewModel() = BackupViewModel(this, object : AnalyticsClient {
        override fun record(event: AnalyticsEvent) { events += event }
        override fun recent(): List<AnalyticsEvent> = events.toList()
    })

    override fun openRecoveryKeySession(mode: BackupRecoveryKeySession.Mode): BackupRecoveryKeySession? =
        if (!available) null else Key(mode).also(sessions::add)

    override fun openBackupDeletionRequest(): BackupDeletionRequest? = if (available) deletionRequest else null
    override suspend fun completeBackupDeletionSelection(request: BackupDeletionRequest, source: Uri) =
        deletionSelection(request, source)

    override fun prepareExport(
        session: BackupRecoveryKeySession,
        selection: Set<BackupWindowHost.ContentClass>,
        onResult: (BackupWindowHost.ExportPreparation) -> Unit,
    ) {
        selections += selection.toSet()
        callbacks += onResult
    }

    override suspend fun writePreparedExport(session: BackupRecoveryKeySession, destination: Uri): BackupExportResult {
        documentCalls++
        return BackupExportResult(BackupDocumentTransfer.WriteResult.VERIFIED)
    }

    override suspend fun copyAndInspectImport(session: BackupRecoveryKeySession, source: Uri): BackupDocumentImport.ImportStatus {
        documentCalls++
        return BackupDocumentImport.ImportStatus.VERIFIED
    }

    override suspend fun abandon(session: BackupRecoveryKeySession) { session.cancel() }

    override fun prepareImportedRestore(session: BackupRecoveryKeySession, targetProfileLabel: String, onResult: (BackupRestorePreparation) -> Unit) {
        restoreRequests += session to targetProfileLabel
        restoreCallbacks += onResult
    }

    override fun confirmAndStageImportedRestore(review: BackupRestoreReview, onResult: (BackupRestoreStageResult) -> Unit) {
        stageReviews += review
        stageCallbacks += onResult
    }

    override fun commitStagedImportedRestore(review: BackupRestoreReview, onResult: (BackupRestoreCommitResult) -> Unit) {
        commitReviews += review
        commitCallbacks += onResult
    }

    override fun resolveImportedRestore(review: BackupRestoreReview, choice: BackupRestoreResolutionChoice, onResult: (BackupRestoreResolutionResult) -> Unit) {
        resolutionChoices += review to choice
        resolutionCallbacks += onResult
    }

    override fun discoverInterruptedRestore(onResult: (BackupRestoreDiscoveryResult) -> Unit): BackupRestoreDiscoveryRequest? {
        discoveryCallbacks += onResult
        duringDiscovery(onResult)
        return discoveryRequest
    }

    override fun abandonImportedRestoreReview(review: BackupRestoreReview) { reviewWithdrawals += review }

    class Key(override val mode: BackupRecoveryKeySession.Mode) : BackupRecoveryKeySession {
        var cancelled = false
        override fun takeGeneratedKeyForDisplay(): CharArray? = null
        override fun confirmKeyRetained() = BackupRecoveryKeySession.Acceptance.ACCEPTED
        override fun acceptEnteredKey(text: CharArray) = BackupRecoveryKeySession.Acceptance.ACCEPTED
        override fun cancel() { cancelled = true }
    }
}
