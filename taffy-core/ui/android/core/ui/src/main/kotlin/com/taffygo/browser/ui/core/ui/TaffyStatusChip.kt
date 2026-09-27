// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.taffyEaseInOut
import com.taffygo.browser.ui.core.designsystem.taffyPingPongPhase
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskDisplayState

/**
 * One status, shown as a word and a shape in one of the seven treatments (the
 * design document's task-state row — the seven, and only these seven).
 *
 * The word is the accessible state description as well as the visible text, so
 * a screen reader hears exactly what a sighted user reads (UX spec section
 * 12.1). The chip is a polite live region, so a change is announced without
 * interrupting whatever is being read. The shape repeats the word's meaning
 * without being announced again.
 */
@Composable
fun TaffyStatusChip(
    presentation: StatusPresentation,
    modifier: Modifier = Modifier,
) {
    val label = taffyString(presentation.labelRes)
    val skin = taffyStatusSkin(presentation.style)
    Row(
        modifier = modifier
            .heightIn(min = ChipHeight)
            .clip(TaffyTheme.shapes.chip)
            .background(skin.background)
            .then(
                if (skin.border != null) {
                    Modifier.border(TaffyBorders.standard, skin.border, TaffyTheme.shapes.chip)
                } else {
                    Modifier
                },
            )
            .padding(horizontal = ChipPadding)
            .semantics {
                liveRegion = LiveRegionMode.Polite
                stateDescription = label
            },
        horizontalArrangement = Arrangement.spacedBy(ChipGap),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (presentation.style == TaffyStatusStyle.RUNNING) {
            PulsingDot(color = skin.content)
        } else if (presentation.icon != null) {
            Icon(
                imageVector = presentation.icon,
                // The shape repeats the word; announcing it twice would be
                // noise, so the glyph is drawn and not described.
                contentDescription = null,
                modifier = Modifier.size(ChipIconSize),
                tint = skin.content,
            )
        }
        Text(
            text = label,
            style = TaffyTheme.typography.caption,
            color = skin.content,
            maxLines = 1,
        )
    }
}

/**
 * The running mark: a six-unit dot breathing at the handoff's `dotPulse` —
 * opacity and scale together, one and a half seconds, ease-in-out.
 */
@Composable
private fun PulsingDot(color: Color) {
    // The handoff's dotPulse, on the shared frame-driven primitive: eased in
    // and out, out and back, because compose-animation-core is not reachable
    // from the fork.
    val phase = taffyPingPongPhase(
        running = !TaffyTheme.reducedMotion,
        periodMillis = DotPulseMs,
    )
    Box(
        modifier = Modifier
            .size(DotSize)
            .graphicsLayer {
                // This state is a layer property, not composition input. Read
                // it here so each frame updates only the six-unit layer rather
                // than recomposing the complete status chip.
                val pulse = taffyEaseInOut(phase.value)
                alpha = DotMinOpacity + (1f - DotMinOpacity) * pulse
                val scale = DotMinScale + (1f - DotMinScale) * pulse
                scaleX = scale
                scaleY = scale
            }
            .background(color, CircleShape),
    )
}

// The spec's geometry (the design document's task-state row; px read as dp).
private val ChipHeight = 26.dp
private val ChipPadding = 10.dp
private val ChipGap = 6.dp
private val ChipIconSize = 11.dp
private val DotSize = 6.dp
private const val DotPulseMs = 1500
private const val DotMinOpacity = 0.35f
private const val DotMinScale = 0.82f
