// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

/**
 * The tone a status is drawn in.
 *
 * A tone is never the whole of a status. Parity row PAR-A11Y-004 says status is
 * not conveyed by colour alone, so every surface that picks a tone also picks a
 * word and a shape — see `StatusPresentation` in `:core:ui`, which makes both
 * of those non-optional at the type level.
 */
enum class TaffyStatusTone {
    /** Nothing is happening, or nothing needs saying. */
    NEUTRAL,

    /** Taffy is doing something right now. */
    ACCENT,

    /** It finished. */
    POSITIVE,

    /** It needs attention, or finished with gaps. */
    CAUTION,

    /** It failed. */
    DANGER,
}
