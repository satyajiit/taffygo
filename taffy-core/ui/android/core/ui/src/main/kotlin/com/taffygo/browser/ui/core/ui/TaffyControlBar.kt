// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.traversalIndex
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskControl

/**
 * The exact controls the task reducer currently allows.
 *
 * They are one component so every surface shows the same closed list, in the same
 * order, with the same words, at the same size. Each is at least a full touch
 * target, and the traversal order puts them ahead of the detail below them so
 * they are never more steps away than any other control on the screen.
 */
@Composable
fun TaffyControlBar(
    controls: List<TaskControl>,
    onControl: (TaskControl) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        controls.forEachIndexed { index, control ->
            OutlinedButton(
                onClick = { onControl(control) },
                enabled = enabled,
                modifier = Modifier
                    .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                    .widthIn(min = TaffyTheme.spacing.minimumTouchTarget)
                    .testTag(controlTestTag(control))
                    .semantics { traversalIndex = index.toFloat() },
            ) {
                Text(
                    text = taffyString(taskControlLabel(control)),
                    style = TaffyTheme.typography.label,
                )
            }
        }
    }
}

/** The test tag one control carries, so a semantics test names it once. */
fun controlTestTag(control: TaskControl): String = "control_${control.label}"

/** One localized label mapping for compact controls and the fixed task footer. */
fun taskControlLabel(control: TaskControl) = when (control) {
    TaskControl.PAUSE -> R.string.taffy_control_pause
    TaskControl.RESUME -> R.string.taffy_control_resume
    TaskControl.STOP -> R.string.taffy_control_stop
    TaskControl.TAKE_OVER -> R.string.taffy_control_take_over
}
