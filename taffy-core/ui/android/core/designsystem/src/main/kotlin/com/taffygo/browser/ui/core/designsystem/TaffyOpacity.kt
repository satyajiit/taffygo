// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

/**
 * The opacities the design handoff names for a colour drawn over another.
 *
 * Fixed values rather than theme tokens, for the reason [TaffyBorders] gives:
 * how much of the page shows through a scrim does not change with the theme,
 * only the scrim's colour does (`TaffyTheme.colors.scrim`). A second scrim
 * colour token would be a second entry in the generated palette and a second
 * contrast row to keep, for what is one number.
 */
object TaffyOpacity {
    /**
     * The Ask overlay's scrim: the page the question is about stays legible
     * through it, dimmed enough that the composer standing on it reads as
     * the thing in front.
     */
    const val ASK_SCRIM: Float = 0.36f

    /**
     * The page held while Taffy works it (`TakeoverInputLock`), at the moment
     * somebody touches it.
     *
     * Lighter than [ASK_SCRIM] on purpose: the Ask overlay asks the person to
     * read a card, and this asks them to watch a page. What is underneath is
     * the point, so the veil only has to say "not yours for the moment".
     *
     * It is no longer a standing veil. It is drawn only while the hold is
     * answering a touch, and it fades out again — a page dimmed for the whole
     * of a task is dimmed for a message the bottom bar is already carrying.
     */
    const val DRIVING_LOCK: Float = 0.14f
}
