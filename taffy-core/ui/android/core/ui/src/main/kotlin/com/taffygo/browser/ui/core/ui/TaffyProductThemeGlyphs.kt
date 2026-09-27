// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import com.taffygo.browser.ui.core.designsystem.TaffyThemeGlyphs

/**
 * The sun and moon the theme change rises, in the product's own icon set.
 *
 * `TaffyThemeGlyphs` exists because the design system cannot reach `TaffyIcon`
 * — it is a layer below it — so the pair is handed down from here, where both
 * halves are visible. This is the one value of it in the product: the
 * application shell passes it, and so does the preview wrapper, which is what
 * makes a transition reviewed in Studio the transition the phone draws.
 *
 * Fill weight, for the reason recorded on [TaffyIcon.SunFill]: the body of a
 * transition is not a control, and it draws far larger than any icon.
 *
 * Lazy, and shared, because an `ImageVector` is immutable and parsing the two
 * paths once is enough — the alternative is re-parsing them on every theme
 * change, which is the frame that can least afford it.
 */
val TaffyProductThemeGlyphs: TaffyThemeGlyphs by lazy {
    TaffyThemeGlyphs(sun = TaffyIcon.SunFill, moon = TaffyIcon.MoonStarsFill)
}
