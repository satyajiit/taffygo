// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.ImageBitmap

/**
 * What a [StartSceneSource] can say about the plate for one moment.
 *
 * Two answers rather than three, which is where this differs from
 * [CountryFlagState] and why on purpose. A flag list draws nothing while it
 * waits, because two hundred rows flashing letters and then pictures is two
 * hundred wrong frames. There is one plate on the start page, it is decorative,
 * and the product ships one compiled in — so the honest thing while the pack is
 * arriving is to draw that one, not a hole where a picture will be.
 */
@Immutable
sealed interface StartSceneState {

    /**
     * No delivered plate — not installed, not published, or not decodable.
     * The caller draws the plate compiled into the installer.
     */
    data object Fallback : StartSceneState

    /** A delivered plate, decoded and ready to draw. */
    data class Ready(val artwork: ImageBitmap) : StartSceneState
}
