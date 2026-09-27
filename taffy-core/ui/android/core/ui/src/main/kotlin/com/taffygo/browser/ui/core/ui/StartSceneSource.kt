// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ProvidableCompositionLocal
import androidx.compose.runtime.Stable
import androidx.compose.runtime.staticCompositionLocalOf

/**
 * Where the start page's painted plate comes from.
 *
 * ## Why this is a port and not an implementation
 *
 * The same reason [CountryFlagSource] is one: there is no image pipeline in
 * this tree — no loader, no cache, no HTTP client on the Android side — and
 * there must not be. Browser chrome does not reach the network. The delivery
 * plane fetches the pack in the browser process against a compiled-in origin
 * and a compiled-in digest, and the shell binds that plane over this port; what
 * happens in composition is a read of bytes that are already on this disk.
 *
 * ## The default draws the compiled-in plate
 *
 * [LocalStartSceneSource] answers [StartSceneState.Fallback] to everything, so
 * a preview, a semantics test, or any host that has not bound the plane draws
 * the plate that ships in the installer and asks for nothing. That is the truth
 * in those graphs rather than a stub, and it is the same picture a phone shows
 * before the pack arrives.
 */
@Stable
interface StartSceneSource {

    /**
     * The plate for this moment, or the instruction to draw the compiled-in one.
     *
     * [targetWidthPx] is the width the plate will actually be drawn at. It is
     * the caller's because only the caller knows it, and it matters because a
     * 640-pixel painting drawn 120 dp wide does not need to be decoded whole —
     * after the decode the memory is already spent.
     */
    @Composable
    fun sceneFor(scene: TaffyStartScene, targetWidthPx: Int): StartSceneState
}

/**
 * The source the start page reads, defaulting to one with no delivered artwork.
 *
 * `static`, not `compositionLocalOf`: the source changes once per process at
 * most — when the shell installs the real one — so the plate does not subscribe
 * to a value that never moves.
 */
val LocalStartSceneSource: ProvidableCompositionLocal<StartSceneSource> =
    staticCompositionLocalOf {
        object : StartSceneSource {
            @Composable
            override fun sceneFor(scene: TaffyStartScene, targetWidthPx: Int): StartSceneState =
                StartSceneState.Fallback
        }
    }
