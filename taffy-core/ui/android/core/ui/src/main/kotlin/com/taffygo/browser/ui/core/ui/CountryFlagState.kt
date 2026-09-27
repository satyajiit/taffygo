// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.ImageBitmap

/**
 * What a [CountryFlagSource] can say about one country's artwork.
 *
 * Three answers rather than a nullable image, because "we have not looked yet",
 * "it is on its way" and "there is none" are three different things on screen:
 * the first two must not flash a fallback that the third one has to keep.
 */
@Immutable
sealed interface CountryFlagState {

    /** No artwork, and none coming. The caller draws its own fallback. */
    data object Absent : CountryFlagState

    /**
     * Artwork is being fetched or decoded.
     *
     * The caller reserves the space and draws nothing in it. It deliberately
     * does not draw the fallback: a flag that appears as two letters and then
     * becomes a picture is two frames of the wrong answer, and a list of
     * countries would do it two hundred times while scrolling.
     */
    data object Loading : CountryFlagState

    /** Decoded artwork, ready to draw. */
    data class Ready(val artwork: ImageBitmap) : CountryFlagState
}
