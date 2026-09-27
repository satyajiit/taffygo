// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem.internal

/**
 * Compose-facing access to the platform-neutral palette projection.
 *
 * Raw token values are owned by `//taffy/resources/tokens/tokens.json` and
 * generated into [GeneratedTaffyPalette]. Keeping this wrapper preserves the
 * Android design-system API while CSS and C++ consume the same source values.
 */
internal object TaffyPalette {
    val light: PaletteValues = GeneratedTaffyPalette.light
    val dark: PaletteValues = GeneratedTaffyPalette.dark
}
