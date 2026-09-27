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
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * A segmented control: one choice of a few, the whole track one recessed well
 * and the chosen segment a raised tab.
 *
 * Each segment is at least the platform's 48-unit touch target, and both the
 * track and its labels can grow at large system font sizes. Used for choices
 * like the Appearance theme switcher, where the options are few and always
 * visible.
 *
 * [optionModifier] lets a caller tag or tweak one segment (a semantics test
 * names the segments it asserts on) without reaching into the track.
 *
 * [optionIcon] is optional and defaults to no glyph, because most of the places
 * this control appears are word-only choices. Where a glyph is given it is
 * decoration: the label beside it already says what the segment selects, and a
 * glyph that repeated it to a screen reader would say everything twice.
 *
 * [optionContentDescription] replaces what a screen reader says for one
 * segment, for the callers whose segment carries more than its label — a count
 * drawn beside the name, say. It is one announcement for the whole segment
 * rather than a second stop after the label, which is why it is given here and
 * not composed beside the control: a bare number the reader arrives at
 * separately says nothing. Returning null leaves the label to speak for
 * itself, which is right wherever the label is all there is.
 */
@Composable
fun TaffySegmentedControl(
    options: List<String>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
    optionModifier: (Int) -> Modifier = { Modifier },
    optionIcon: (Int) -> ImageVector? = { null },
    optionContentDescription: (Int) -> String? = { null },
) {
    Row(
        modifier = modifier
            .heightIn(min = TrackMinimumHeight)
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.surfaceSunken)
            .padding(TaffyTheme.spacing.step)
            .selectableGroup(),
    ) {
        options.forEachIndexed { index, option ->
            val isSelected = index == selectedIndex
            Box(
                modifier = Modifier
                    .weight(1f)
                    .then(optionModifier(index))
                    .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                    .clip(TaffyTheme.shapes.pill)
                    .then(
                        if (isSelected) {
                            Modifier
                                .background(TaffyTheme.colors.surfaceRaised)
                                .border(
                                    TaffyBorders.standard,
                                    TaffyTheme.colors.outline,
                                    TaffyTheme.shapes.pill,
                                )
                        } else {
                            Modifier
                        },
                    )
                    .selectable(
                        selected = isSelected,
                        role = Role.Tab,
                        onClick = { onSelect(index) },
                    )
                    .padding(
                        horizontal = TaffyTheme.spacing.tight,
                        vertical = TaffyTheme.spacing.step,
                    )
                    .then(
                        optionContentDescription(index)?.let { spoken ->
                            Modifier.semantics(mergeDescendants = true) {
                                contentDescription = spoken
                            }
                        } ?: Modifier,
                    ),
                contentAlignment = Alignment.Center,
            ) {
                val ink = if (isSelected) {
                    TaffyTheme.colors.textPrimary
                } else {
                    TaffyTheme.colors.textSecondary
                }
                Row(
                    horizontalArrangement = Arrangement.spacedBy(
                        TaffyTheme.spacing.step,
                        Alignment.CenterHorizontally,
                    ),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    optionIcon(index)?.let { glyph ->
                        Icon(
                            imageVector = glyph,
                            contentDescription = null,
                            tint = ink,
                            modifier = Modifier.size(SegmentGlyphSize),
                        )
                    }
                    Text(
                        text = option,
                        style = TaffyTheme.typography.label,
                        color = ink,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                        textAlign = TextAlign.Center,
                    )
                }
            }
        }
    }
}

// A four-unit well around a 48-unit target makes the track 56 units tall.
private val TrackMinimumHeight = 56.dp

// Mock 04's segment glyph.
private val SegmentGlyphSize = 15.dp
