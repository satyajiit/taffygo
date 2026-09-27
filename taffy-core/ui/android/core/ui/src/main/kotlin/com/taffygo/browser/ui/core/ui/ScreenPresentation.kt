// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * What the shell draws for one back stack: the screen that owns the frame,
 * and the overlay standing over it, if any.
 *
 * A destination that [TaffyDestination.presentsOverBase] does not take the
 * frame. The nearest screen beneath it keeps drawing — for the Ask overlay
 * that is the page the question is about, which stays visible through the
 * overlay's scrim — and the overlay is composed over it. The overlay is still
 * an entry on the stack, so its view model, its saved question and its
 * analytics are exactly what they were when it was drawn as a whole screen;
 * only where it is drawn changed.
 */
data class ScreenPresentation(
    /** The destination that owns the frame. Never one that presents over another. */
    val base: TaffyDestination,
    /** The overlay over [base], or null when the current entry is a screen. */
    val overlay: TaffyDestination? = null,
)

/**
 * This stack's presentation.
 *
 * A screen pushed over an overlay takes the frame and the overlay waits
 * beneath it, which is what lets "Set up Taffy" from the Ask overlay return
 * to the overlay on back. A stack of nothing but overlays stands on the start
 * destination, so a restored stack that begins with an overlay still has a
 * screen under it rather than a blank frame.
 */
fun BackStack.presentation(): ScreenPresentation {
    val base = entries.lastOrNull { !it.presentsOverBase } ?: TaffyDestination.START
    val overlay = current.takeIf { it.presentsOverBase }
    return ScreenPresentation(base = base, overlay = overlay)
}
