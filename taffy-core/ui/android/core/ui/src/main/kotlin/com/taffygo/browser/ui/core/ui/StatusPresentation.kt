// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.annotation.StringRes
import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.designsystem.TaffyStatusTone
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.model.TaskDisplayState

/**
 * How one status is shown: a word, a shape, and a tone — all three, always.
 *
 * Parity row PAR-A11Y-004 says status is never conveyed by colour alone. That
 * is a property of this type rather than a rule a screen has to remember: there
 * is no way to construct a presentation without a label and a shape, so a
 * screen that draws a status draws both.
 *
 * The shape is a [TaffyIcon] glyph — except RUNNING, whose shape is the pulsing
 * dot the chip draws itself, so its [icon] is null on purpose.
 */
@Immutable
data class StatusPresentation(
    /** The word shown and announced. */
    @param:StringRes val labelRes: Int,
    /** A shape that carries the same meaning as the tone, for anyone who cannot use it. */
    val icon: ImageVector?,
    /** The treatment the chip wears (background, border, and ink). */
    val style: TaffyStatusStyle,
    /** The tone the word and shape are drawn in. */
    val tone: TaffyStatusTone,
) {
    companion object {
        /** How each of the seven user-visible task states is shown. */
        fun of(state: TaskDisplayState): StatusPresentation = when (state) {
            TaskDisplayState.RUNNING ->
                StatusPresentation(
                    R.string.taffy_task_state_running,
                    null,
                    TaffyStatusStyle.RUNNING,
                    TaffyStatusTone.ACCENT,
                )
            TaskDisplayState.WAITING_FOR_YOU ->
                StatusPresentation(
                    R.string.taffy_task_state_waiting_for_you,
                    TaffyIcon.Hand,
                    TaffyStatusStyle.WAITING,
                    TaffyStatusTone.CAUTION,
                )
            TaskDisplayState.PAUSED ->
                StatusPresentation(
                    R.string.taffy_task_state_paused,
                    TaffyIcon.Pause,
                    TaffyStatusStyle.PAUSED,
                    TaffyStatusTone.NEUTRAL,
                )
            TaskDisplayState.DONE ->
                StatusPresentation(
                    R.string.taffy_task_state_done,
                    TaffyIcon.CheckCircle,
                    TaffyStatusStyle.DONE,
                    TaffyStatusTone.POSITIVE,
                )
            TaskDisplayState.PARTLY_DONE ->
                StatusPresentation(
                    R.string.taffy_task_state_partly_done,
                    TaffyIcon.CircleHalf,
                    TaffyStatusStyle.PARTLY_DONE,
                    TaffyStatusTone.CAUTION,
                )
            TaskDisplayState.STOPPED ->
                StatusPresentation(
                    R.string.taffy_task_state_stopped,
                    TaffyIcon.StopCircle,
                    TaffyStatusStyle.STOPPED,
                    TaffyStatusTone.NEUTRAL,
                )
            TaskDisplayState.FAILED ->
                StatusPresentation(
                    R.string.taffy_task_state_failed,
                    TaffyIcon.Warning,
                    TaffyStatusStyle.FAILED,
                    TaffyStatusTone.DANGER,
                )
        }

        /** How each download state is shown. */
        fun of(state: DownloadState): StatusPresentation = when (state) {
            DownloadState.RUNNING ->
                StatusPresentation(
                    R.string.taffy_download_state_running,
                    null,
                    TaffyStatusStyle.RUNNING,
                    TaffyStatusTone.ACCENT,
                )
            DownloadState.PAUSED ->
                StatusPresentation(
                    R.string.taffy_download_state_paused,
                    TaffyIcon.Pause,
                    TaffyStatusStyle.PAUSED,
                    TaffyStatusTone.NEUTRAL,
                )
            DownloadState.COMPLETE ->
                StatusPresentation(
                    R.string.taffy_download_state_complete,
                    TaffyIcon.CheckCircle,
                    TaffyStatusStyle.DONE,
                    TaffyStatusTone.POSITIVE,
                )
            DownloadState.FAILED ->
                StatusPresentation(
                    R.string.taffy_download_state_failed,
                    TaffyIcon.Warning,
                    TaffyStatusStyle.FAILED,
                    TaffyStatusTone.DANGER,
                )
        }
    }
}
