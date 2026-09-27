// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Shape

/**
 * The Soft Pulse corner-radius ramp.
 *
 * TaffyGo is flat: separation between surfaces comes from 1dp outline borders
 * and tonal steps, never from shadows, so there are deliberately no elevation
 * tokens here or anywhere else in the design system.
 *
 *   chip   8  — small inner chips, tag pills, glyph wells under 44 dp
 *   row   10  — list rows and pressable rows
 *   tile  14  — icon tiles, inputs, nested controls, stat tiles
 *   card  18  — object cards
 *   sheet 24  — bottom sheets (top corners) and hero panels ([hero] all sides)
 *   pill 50%  — fully rounded ends
 *
 * Every figure above comes from [TaffyRadii] rather than being written here,
 * so a surface and a hand-drawn outline of that surface cannot disagree.
 */
@Immutable
data class TaffyShapes(
    /** Chips, tags, and the 26 dp stat glyph host. */
    val chip: Shape = RoundedCornerShape(TaffyRadii.chip),
    /** List rows. */
    val row: Shape = RoundedCornerShape(TaffyRadii.row),
    /** Icon tiles and nested controls. */
    val tile: Shape = RoundedCornerShape(TaffyRadii.tile),
    /** Object cards. */
    val card: Shape = RoundedCornerShape(TaffyRadii.card),
    /** Sheets, rounded on the top corners only. */
    val sheet: Shape = RoundedCornerShape(
        topStart = TaffyRadii.sheet,
        topEnd = TaffyRadii.sheet,
    ),
    /** Hero panels, rounded on every corner. */
    val hero: Shape = RoundedCornerShape(TaffyRadii.sheet),
    /** Pills: fully rounded ends. */
    val pill: Shape = RoundedCornerShape(percent = 50),
    /** Buttons: fully rounded (half the height). */
    val button: Shape = RoundedCornerShape(percent = 50),
    /** Switch tracks. */
    val switch: Shape = RoundedCornerShape(percent = 50),
) {
    companion object {
        /** The one set of radii. Shared so the theme does not allocate. */
        val Default: TaffyShapes = TaffyShapes()
    }
}
