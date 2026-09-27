// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.vector.ImageVector

/**
 * The two glyphs the theme change rises, handed in from above.
 *
 * The transition wants the product's real sun and moon, and cannot reach them:
 * `TaffyIcon` lives in `:core:ui`, which is layer 40, and this module is layer
 * 30. Reading upward is precisely what the layer rule exists to stop, and the
 * two ways around it are both worse than a parameter — moving `TaffyIcon` down
 * here would drag its vendor register, its pin row and every import in the tree
 * with it, and moving `TaffyTheme` up into `:core:ui` would invert the twelve
 * modules that read `TaffyTheme.colors`.
 *
 * So the glyphs arrive as a value instead. `ImageVector` is
 * `compose-ui-graphics`, which this module already depends on, so carrying the
 * pair costs no new edge and no change to the component graph.
 *
 * Both the application shell and the preview wrapper pass the same pair, which
 * is what keeps a preview honest: a transition reviewed in Studio is drawing
 * the glyph the phone will draw. A caller that passes nothing gets the drawn
 * fallback shapes, so nothing is ever missing — only less exact.
 */
@Immutable
class TaffyThemeGlyphs(val sun: ImageVector, val moon: ImageVector)
