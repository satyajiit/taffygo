// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Press scale for large cards and 2-up tiles. */
const val TAFFY_PRESS_SCALE: Float = 0.97f

/**
 * A pressable that scales in, then back, and holds still when motion is off.
 */
@Composable
fun TaffyPressable(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    role: Role = Role.Button,
    scaleTo: Float = TAFFY_PRESS_SCALE,
    testTag: String? = null,
    content: @Composable () -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val scale = if (!TaffyTheme.reducedMotion && pressed) scaleTo else 1f
    Box(
        modifier = modifier
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
            }
            .clickable(
                interactionSource = interaction,
                indication = null,
                enabled = enabled,
                role = role,
                onClick = onClick,
            )
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
    ) {
        content()
    }
}
