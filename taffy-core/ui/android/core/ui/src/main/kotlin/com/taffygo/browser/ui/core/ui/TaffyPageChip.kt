// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyEaseOut
import com.taffygo.browser.ui.core.designsystem.taffyRunOnce

/**
 * One page in the list Taffy will look at.
 *
 * Captions and the remove name are parameters: this module does not own Ask
 * copy. Taffy-opened chips take a 2 dp accent edge and no remove. Closed chips
 * keep the title, speak the closed caption in caution, and are not tappable
 * to open. Motion is [taffyRunOnce]; reduced motion snaps.
 */
@Composable
fun TaffyPageChip(
    state: TaffyPageChipState,
    taffyOpenedCaption: String,
    closedCaption: String,
    removeContentDescription: String,
    modifier: Modifier = Modifier,
    onRemove: (() -> Unit)? = null,
    onClick: (() -> Unit)? = null,
    testTag: String? = null,
) {
    val reducedMotion = TaffyTheme.reducedMotion
    var exiting by remember { mutableStateOf(false) }
    val addProgress by taffyRunOnce(
        running = !reducedMotion && !exiting,
        durationMillis = AddMillis,
    )
    val removeProgress by taffyRunOnce(
        running = !reducedMotion && exiting,
        durationMillis = RemoveMillis,
    )
    val removalFinished = removeProgress >= 1f
    LaunchedEffect(exiting, removalFinished) {
        if (exiting && removalFinished) onRemove?.invoke()
    }

    val scale: Float
    val alpha: Float
    when {
        reducedMotion -> {
            scale = 1f
            alpha = 1f
        }
        exiting -> {
            scale = 1f - 0.12f * removeProgress
            alpha = 1f - removeProgress
        }
        else -> {
            val eased = taffyEaseOut(addProgress)
            scale = 0.92f + 0.08f * eased
            alpha = eased
        }
    }

    val titleLine = state.title.ifBlank { state.host }
    val caption = when {
        state.closed -> closedCaption
        state.taffyOpened -> taffyOpenedCaption
        state.title.isNotBlank() -> state.host
        else -> ""
    }
    val spoken = if (caption.isEmpty()) {
        titleLine
    } else {
        taffyString(R.string.taffy_accessible_pair, titleLine, caption)
    }
    val showRemove = state.removable && !state.taffyOpened && onRemove != null
    val borderWidth = if (state.taffyOpened) TaffyBorders.rail else TaffyBorders.standard
    val borderColor =
        if (state.taffyOpened) TaffyTheme.colors.accent else TaffyTheme.colors.outline
    val shape = TaffyTheme.shapes.row
    val requestRemove: () -> Unit = {
        if (!exiting) {
            if (reducedMotion) onRemove?.invoke() else exiting = true
        }
    }

    Row(
        modifier = modifier
            .fillMaxWidth()
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
                this.alpha = alpha
            }
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .clip(shape)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(borderWidth, borderColor, shape),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier
                .weight(1f)
                .then(
                    if (onClick != null && !state.closed) {
                        Modifier.clickable(role = Role.Button, onClick = onClick)
                    } else {
                        Modifier
                    },
                )
                .padding(
                    start = TaffyTheme.spacing.snug,
                    top = TaffyTheme.spacing.tight,
                    bottom = TaffyTheme.spacing.tight,
                    end = if (showRemove) 0.dp else TaffyTheme.spacing.snug,
                )
                .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
                .semantics(mergeDescendants = true) { contentDescription = spoken },
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            Box(modifier = Modifier.graphicsLayer { this.alpha = if (state.closed) 0.4f else 1f }) {
                TaffyGlyphFrame {
                    val mark = state.mark
                    if (mark != null) {
                        Image(
                            bitmap = mark,
                            contentDescription = null,
                            contentScale = ContentScale.Crop,
                            modifier = Modifier.fillMaxSize(),
                        )
                    } else {
                        val initial = state.host.firstOrNull()?.uppercaseChar()?.toString()
                            ?: state.title.firstOrNull()?.uppercaseChar()?.toString()
                            ?: ""
                        if (initial.isNotEmpty()) {
                            Text(
                                text = initial,
                                style = TaffyTheme.typography.title,
                                color = TaffyTheme.colors.textSecondary,
                            )
                        }
                    }
                }
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(
                    text = titleLine,
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textPrimary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (caption.isNotEmpty()) {
                    Text(
                        text = caption,
                        style = TaffyTheme.typography.caption,
                        color = if (state.closed) {
                            TaffyTheme.colors.caution
                        } else {
                            TaffyTheme.colors.textSecondary
                        },
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
        if (showRemove) {
            Box(
                modifier = Modifier
                    .size(TaffyTheme.spacing.minimumTouchTarget)
                    .clickable(role = Role.Button, onClick = requestRemove)
                    .semantics { contentDescription = removeContentDescription },
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    imageVector = TaffyIcon.X,
                    contentDescription = null,
                    tint = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.size(RemoveIconSize),
                )
            }
        }
    }
}

private const val AddMillis = 180
private const val RemoveMillis = 140
private val RemoveIconSize = 20.dp
