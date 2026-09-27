// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.annotation.AnyThread
import androidx.annotation.UiThread
import androidx.annotation.WorkerThread
import java.io.Closeable

/** One exact Chromium backup window. Each method declares its actual thread contract. */
interface BackupNativeWindow : Closeable {
    @UiThread
    fun activate(): Boolean
    @UiThread
    fun deactivate()
    @UiThread
    fun beginRecoveryKeySession(mode: Int): String?
    @UiThread
    fun takeGeneratedKeyForDisplay(operationId: String): CharArray?
    @UiThread
    fun confirmKeyRetained(operationId: String): Int
    @UiThread
    fun acceptEnteredKey(operationId: String, key: CharArray): Int
    @UiThread
    fun prepareExport(
        operationId: String,
        selection: IntArray,
        callback: (NativeExportPreparation) -> Unit,
    ): Boolean
    @UiThread
    fun maximumImportBytes(operationId: String): Long
    @WorkerThread
    fun openEncryptedArchiveReadFd(operationId: String): Int
    @WorkerThread
    fun openReadbackWriteFd(operationId: String, expectedBytes: Long): Int
    @WorkerThread
    fun verifyEncryptedReadback(operationId: String): Int
    @WorkerThread
    fun openEncryptedImportWriteFd(operationId: String, maximumBytes: Long): Int
    @WorkerThread
    fun inspectImportedArchive(operationId: String, actualBytes: Long): Int
    @UiThread
    fun prepareImportedRestore(
        operationId: String,
        targetProfileLabel: String,
        callback: (NativeRestorePreparation) -> Unit,
    ): Boolean
    @UiThread
    fun confirmAndStageRestore(reviewToken: Long, callback: (Int) -> Unit): Boolean
    @UiThread
    fun commitRestore(reviewToken: Long, callback: (Int) -> Unit): Boolean
    @UiThread
    fun resolveRestore(reviewToken: Long, choice: Int, callback: (Int) -> Unit): Boolean
    @UiThread
    fun discoverInterruptedRestore(
        callback: (NativeRestoreDiscovery) -> Unit,
    ): NativeRestoreDiscoveryRequest?
    @UiThread
    fun resolveRecoveredRestore(
        recoveredReviewToken: Long,
        choice: Int,
        callback: (Int) -> Unit,
    ): Boolean
    @UiThread
    fun abandonRecoveredRestoreReview(recoveredReviewToken: Long)

    /**
     * Withdraws from UI or I/O cleanup. Off-UI callers block for UI settlement and must hold no
     * lock needed by the UI thread.
     */
    @AnyThread
    fun abandon(operationId: String)

    @UiThread
    override fun close()
}
