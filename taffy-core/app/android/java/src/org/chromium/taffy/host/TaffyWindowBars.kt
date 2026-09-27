// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

/**
 * How the composition tells the window which ground its bars stand on.
 *
 * WHY THIS EXISTS AS AN INTERFACE. It is the same split as [TaffyFirstRunSignal]
 * and for the same reason: `//taffy/app/android/DEPS` rule 1 keeps this
 * package off `//chrome`, and the object that can write a system bar's colour —
 * `EdgeToEdgeSystemBarColorHelper`, reachable only through
 * `ChromeBaseAppCompatActivity.initializeSystemBarColors` — belongs to the one
 * directory allowed the exception, `//taffy/app/android/shell`. So the
 * *decision* is here, where the theme is resolved, and the *write* is in
 * `TaffyBrowserActivity`, where `//chrome` may be named. Neither half can
 * contradict the other, because only one of them decides.
 *
 * WHY A COLOUR AND NOT A BOOLEAN. Screen SCR-407 lets the Appearance setting
 * disagree with the device, so the polarity may not be read off a night
 * qualifier — but it also may not be read off a second opinion about what
 * "dark" looks like. This carries the surface the composition is actually
 * painting, so the icons are chosen from the paint they will sit on.
 *
 * A `fun interface`, so the activity passes a method reference from Java and
 * the whole of its side is one argument to [TaffyShellViews.of].
 */
fun interface TaffyWindowBars {

    /**
     * The window's ground has been resolved, or has changed.
     *
     * Called on the UI thread from composition, and safe to call repeatedly:
     * the helper behind it drops a colour it already holds.
     *
     * @param argb the opaque surface colour of the theme now in scope.
     */
    fun onTaffySurfaceColor(argb: Int)
}
