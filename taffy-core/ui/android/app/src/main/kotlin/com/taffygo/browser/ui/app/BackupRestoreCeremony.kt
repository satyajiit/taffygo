// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.annotation.MainThread
import java.io.Closeable
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Local screen sequencing, not a restore protocol implementation. Every action
 * still crosses the native host, which alone holds the reviewed plan and must
 * obtain current source-Core authority. No success is inferred from a UI step.
 */
@MainThread
internal class BackupRestoreCeremony(
    private val host: BackupWindowHost,
    private val scope: CoroutineScope,
    publish: (BackupRestoreUiState) -> Unit,
    finish: (BackupUiState.Notice) -> Unit,
) : Closeable {
    private var publish: ((BackupRestoreUiState) -> Unit)? = publish
    private var finish: ((BackupUiState.Notice) -> Unit)? = finish
    private var closed = false
    private var started = false
    private var recovered = false
    private var review: BackupRestoreReview? = null
    private var resolutionAttempt: Any? = null
    private var local = BackupRestoreUiState()

    fun start(session: BackupRecoveryKeySession, targetProfileLabel: String) {
        if (closed || started) return
        started = true
        publish?.invoke(local)
        if (!isAt(BackupRestoreUiState.Step.PLANNING)) return
        try {
            host.prepareImportedRestore(session, targetProfileLabel) { result ->
                scope.launch {
                    if (!isAt(BackupRestoreUiState.Step.PLANNING)) return@launch
                    when (result) {
                        is BackupRestorePreparation.Ready -> {
                            val summary = try {
                                result.review.summary.let {
                                    it.copy(selectedClasses = it.selectedClasses.toList())
                                }
                            } catch (_: RuntimeException) {
                                end(BackupUiState.Notice.UNAVAILABLE)
                                return@launch
                            }
                            review = result.review
                            update(BackupRestoreUiState(
                                BackupRestoreUiState.Step.REVIEW,
                                summary,
                            ))
                        }
                        BackupRestorePreparation.Refused -> end(BackupUiState.Notice.RESTORE_REFUSED)
                        BackupRestorePreparation.Unavailable -> end(BackupUiState.Notice.UNAVAILABLE)
                    }
                }
            }
        } catch (_: RuntimeException) {
            if (isAt(BackupRestoreUiState.Step.PLANNING)) end(BackupUiState.Notice.UNAVAILABLE)
        }
    }

    /** Adopts fresh native review custody; never enters planning, staging or commit. */
    fun startRecovered(owned: BackupRestoreReview, cleanupOnly: Boolean) {
        if (closed || started) {
            host.abandonImportedRestoreReview(owned)
            return
        }
        started = true
        recovered = true
        review = owned
        val summary = try {
            owned.summary.let { it.copy(selectedClasses = it.selectedClasses.map { row -> row.copy() }) }
        } catch (_: RuntimeException) {
            end(BackupUiState.Notice.RECOVERY_UNAVAILABLE)
            return
        }
        update(BackupRestoreUiState(
            step = if (cleanupOnly) BackupRestoreUiState.Step.CLEANUP_REQUIRED else BackupRestoreUiState.Step.CANDIDATE,
            summary = summary,
            cleanupOnly = cleanupOnly,
            recovered = true,
        ))
    }

    fun onIntent(intent: BackupIntent, resumed: Boolean) {
        if (closed || !resumed) return
        when (intent) {
            BackupIntent.StageRestore -> stage()
            BackupIntent.CommitRestore -> commit()
            BackupIntent.AcceptRestore -> if (isAt(BackupRestoreUiState.Step.CANDIDATE)) {
                resolve(BackupRestoreResolutionChoice.ACCEPT)
            }
            BackupIntent.RequestDiscardRestore -> if (isAt(BackupRestoreUiState.Step.CANDIDATE) ||
                isAt(BackupRestoreUiState.Step.CLEANUP_REQUIRED)
            ) {
                move(BackupRestoreUiState.Step.CONFIRM_DISCARD)
            }
            BackupIntent.BackToRestoreReview -> if (isAt(BackupRestoreUiState.Step.CONFIRM_DISCARD)) {
                move(reviewStep())
            }
            BackupIntent.DiscardRestore -> if (isAt(BackupRestoreUiState.Step.CONFIRM_DISCARD)) {
                resolve(BackupRestoreResolutionChoice.DISCARD)
            }
            else -> Unit
        }
    }

    override fun close() {
        if (closed) return
        closed = true
        val owned = review
        review = null
        resolutionAttempt = null
        publish = null
        finish = null
        if (recovered && owned != null) host.abandonImportedRestoreReview(owned)
        // The parent owns a live import's session withdrawal; recovered reviews own no session.
        // After dispatch, native custody drains the terminal; closing this view cannot undo it.
    }

    private fun stage() {
        val owned = review ?: return
        if (!isAt(BackupRestoreUiState.Step.REVIEW) || local.summary?.canStage != true ||
            local.summary?.hasConflicts != false
        ) return
        move(BackupRestoreUiState.Step.STAGING)
        if (!isAt(BackupRestoreUiState.Step.STAGING) || review !== owned) return
        try {
            host.confirmAndStageImportedRestore(owned) { result ->
                scope.launch {
                    if (!isAt(BackupRestoreUiState.Step.STAGING)) return@launch
                    when (result) {
                        BackupRestoreStageResult.STAGED -> move(BackupRestoreUiState.Step.STAGED)
                        BackupRestoreStageResult.REFUSED -> end(BackupUiState.Notice.RESTORE_REFUSED)
                        BackupRestoreStageResult.UNAVAILABLE -> end(BackupUiState.Notice.UNAVAILABLE)
                    }
                }
            }
        } catch (_: RuntimeException) {
            if (isAt(BackupRestoreUiState.Step.STAGING)) end(BackupUiState.Notice.UNAVAILABLE)
        }
    }

    private fun commit() {
        val owned = review ?: return
        if (!isAt(BackupRestoreUiState.Step.STAGED)) return
        move(BackupRestoreUiState.Step.COMMITTING)
        if (!isAt(BackupRestoreUiState.Step.COMMITTING) || review !== owned) return
        try {
            host.commitStagedImportedRestore(owned) { result ->
                scope.launch {
                    if (!isAt(BackupRestoreUiState.Step.COMMITTING)) return@launch
                    when (result) {
                        BackupRestoreCommitResult.HIDDEN_CANDIDATE -> move(BackupRestoreUiState.Step.CANDIDATE)
                        BackupRestoreCommitResult.DEFINITELY_NOT_COMMITTED -> update(local.copy(
                            step = BackupRestoreUiState.Step.CLEANUP_REQUIRED,
                            cleanupOnly = true,
                        ))
                        // Even a refusal after the call began is not UI proof
                        // that no consumptive request escaped this process.
                        else -> move(BackupRestoreUiState.Step.NEEDS_RECOVERY)
                    }
                }
            }
        } catch (_: RuntimeException) {
            if (isAt(BackupRestoreUiState.Step.COMMITTING)) move(BackupRestoreUiState.Step.NEEDS_RECOVERY)
        }
    }

    private fun resolve(choice: BackupRestoreResolutionChoice) {
        val owned = review ?: return
        val attempt = Any()
        resolutionAttempt = attempt
        update(local.copy(step = BackupRestoreUiState.Step.RESOLVING, choiceNotCompleted = false))
        if (!isAt(BackupRestoreUiState.Step.RESOLVING) || review !== owned || resolutionAttempt !== attempt) return
        try {
            host.resolveImportedRestore(owned, choice) { result ->
                scope.launch {
                    if (!isAt(BackupRestoreUiState.Step.RESOLVING) || resolutionAttempt !== attempt) return@launch
                    resolutionAttempt = null
                    when {
                        result == BackupRestoreResolutionResult.PUBLISHED &&
                            choice == BackupRestoreResolutionChoice.ACCEPT -> end(BackupUiState.Notice.RESTORE_KEPT)
                        result == BackupRestoreResolutionResult.VERIFIED_DELETED &&
                            choice == BackupRestoreResolutionChoice.DISCARD -> end(BackupUiState.Notice.RESTORE_DISCARDED)
                        result in setOf(
                            BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED,
                            BackupRestoreResolutionResult.REFUSED,
                            BackupRestoreResolutionResult.UNAVAILABLE,
                        ) -> update(local.copy(
                            step = reviewStep(),
                            choiceNotCompleted = true,
                        ))
                        else -> move(BackupRestoreUiState.Step.NEEDS_RECOVERY)
                    }
                }
            }
        } catch (_: RuntimeException) {
            if (isAt(BackupRestoreUiState.Step.RESOLVING) && resolutionAttempt === attempt) {
                resolutionAttempt = null
                move(BackupRestoreUiState.Step.NEEDS_RECOVERY)
            }
        }
    }

    private fun isAt(step: BackupRestoreUiState.Step) = !closed && scope.isActive && local.step == step

    private fun reviewStep() = if (local.cleanupOnly) BackupRestoreUiState.Step.CLEANUP_REQUIRED
        else BackupRestoreUiState.Step.CANDIDATE

    private fun move(step: BackupRestoreUiState.Step) = update(local.copy(step = step))

    private fun update(state: BackupRestoreUiState) {
        if (closed) return
        local = state
        publish?.invoke(state)
    }

    private fun end(notice: BackupUiState.Notice) {
        if (closed) return
        val onFinished = finish
        close()
        onFinished?.invoke(notice)
    }
}
