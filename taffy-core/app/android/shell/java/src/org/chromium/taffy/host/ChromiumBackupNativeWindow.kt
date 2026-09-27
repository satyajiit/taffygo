// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupWindowHost
import org.chromium.taffy.browser.TaffyBackupWorkflowBridge
import org.chromium.taffy.core_service.mojom.BackupRecordKind

internal fun TaffyBackupWorkflowBridge.Window.asBackupNativeWindow(): BackupNativeWindow =
    object : BackupNativeWindow {
        override fun activate() = this@asBackupNativeWindow.activate()
        override fun deactivate() = this@asBackupNativeWindow.deactivate()
        override fun beginRecoveryKeySession(mode: Int) =
            this@asBackupNativeWindow.beginRecoveryKeySession(mode)
        override fun takeGeneratedKeyForDisplay(operationId: String) =
            this@asBackupNativeWindow.takeGeneratedKeyForDisplay(operationId)
        override fun confirmKeyRetained(operationId: String) =
            this@asBackupNativeWindow.confirmKeyRetained(operationId)
        override fun acceptEnteredKey(operationId: String, key: CharArray) =
            this@asBackupNativeWindow.acceptEnteredKey(operationId, key)
        override fun prepareExport(
            operationId: String,
            selection: IntArray,
            callback: (NativeExportPreparation) -> Unit,
        ) = this@asBackupNativeWindow.prepareExport(operationId, selection) {
            callback(NativeExportPreparation(it.archiveBytes(), it.status()))
        }
        override fun maximumImportBytes(operationId: String) =
            this@asBackupNativeWindow.maximumImportBytes(operationId)
        override fun openEncryptedArchiveReadFd(operationId: String) =
            this@asBackupNativeWindow.openEncryptedArchiveReadFd(operationId)
        override fun openReadbackWriteFd(operationId: String, expectedBytes: Long) =
            this@asBackupNativeWindow.openReadbackWriteFd(operationId, expectedBytes)
        override fun verifyEncryptedReadback(operationId: String) =
            this@asBackupNativeWindow.verifyEncryptedReadback(operationId)
        override fun openEncryptedImportWriteFd(operationId: String, maximumBytes: Long) =
            this@asBackupNativeWindow.openEncryptedImportWriteFd(operationId, maximumBytes)
        override fun inspectImportedArchive(operationId: String, actualBytes: Long) =
            this@asBackupNativeWindow.inspectImportedArchive(operationId, actualBytes)
        override fun prepareImportedRestore(
            operationId: String,
            targetProfileLabel: String,
            callback: (NativeRestorePreparation) -> Unit,
        ) = this@asBackupNativeWindow.restore().prepareImportedRestore(
            operationId,
            targetProfileLabel,
        ) {
            callback(
                NativeRestorePreparation(
                    it.reviewToken(),
                    it.targetProfileLabel(),
                    it.flattenedClassCounts().copyOf(),
                    it.hasConflicts(),
                    it.canStage(),
                    it.status(),
                ),
            )
        }
        override fun confirmAndStageRestore(reviewToken: Long, callback: (Int) -> Unit) =
            this@asBackupNativeWindow.restore().confirmAndStageRestore(reviewToken) { callback(it) }
        override fun commitRestore(reviewToken: Long, callback: (Int) -> Unit) =
            this@asBackupNativeWindow.restore().commitRestore(reviewToken) { callback(it) }
        override fun resolveRestore(reviewToken: Long, choice: Int, callback: (Int) -> Unit) =
            this@asBackupNativeWindow.restore().resolveRestore(reviewToken, choice) { callback(it) }
        override fun discoverInterruptedRestore(
            callback: (NativeRestoreDiscovery) -> Unit,
        ): NativeRestoreDiscoveryRequest? {
            val request = this@asBackupNativeWindow.restore().discoverInterruptedRestore {
                callback(
                    NativeRestoreDiscovery(
                        it.recoveredReviewToken(),
                        it.targetProfileLabel(),
                        it.flattenedClassCounts().copyOf(),
                        it.hasConflicts(),
                        it.canStage(),
                        it.cleanupOnly(),
                        it.status(),
                    ),
                )
            } ?: return null
            return object : NativeRestoreDiscoveryRequest {
                override fun close() = request.close()
            }
        }
        override fun resolveRecoveredRestore(
            recoveredReviewToken: Long,
            choice: Int,
            callback: (Int) -> Unit,
        ) = this@asBackupNativeWindow.restore().resolveRecoveredRestore(
            recoveredReviewToken,
            choice,
        ) { callback(it) }
        override fun abandonRecoveredRestoreReview(recoveredReviewToken: Long) =
            this@asBackupNativeWindow.restore()
                .abandonRecoveredRestoreReview(recoveredReviewToken)
        override fun abandon(operationId: String) = this@asBackupNativeWindow.abandon(operationId)
        override fun close() = this@asBackupNativeWindow.close()
    }

internal fun BackupRecoveryKeySession.Mode.toNative() = when (this) {
    BackupRecoveryKeySession.Mode.CREATE -> TaffyBackupWorkflowBridge.MODE_CREATE
    BackupRecoveryKeySession.Mode.RESTORE -> TaffyBackupWorkflowBridge.MODE_RESTORE
}

internal fun BackupWindowHost.ContentClass.toWire() = when (this) {
    BackupWindowHost.ContentClass.ASSISTANT_CONFIGURATION -> BackupRecordKind.ASSISTANT_CONFIGURATION
    BackupWindowHost.ContentClass.SAVED_WORKSPACES -> BackupRecordKind.SAVED_WORKSPACE
    BackupWindowHost.ContentClass.LIBRARY -> BackupRecordKind.LIBRARY_ENTRY
    BackupWindowHost.ContentClass.MEMORY -> BackupRecordKind.MEMORY_RECORD
    BackupWindowHost.ContentClass.USER_AUTHORED_SKILLS -> BackupRecordKind.USER_AUTHORED_SKILL
    BackupWindowHost.ContentClass.LEARNED_PROCEDURES -> BackupRecordKind.LEARNED_PROCEDURE
}
