// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.LocalOnBackPressedDispatcherOwner
import androidx.compose.runtime.Composable

/**
 * Answer the system back button or gesture while [enabled], and let it pass
 * while it is not.
 *
 * ## Why this is a function of the UI layer's own and not `BackHandler` inline
 *
 * Two reasons, and the second is the one that bites.
 *
 * `BackHandler` throws when there is no [LocalOnBackPressedDispatcherOwner] —
 * and TaffyGo's surfaces are composed without one all the time: in every
 * `@Preview`, and in the semantics tests that render a screen's stateless half
 * on its own. So the owner is read rather than assumed, once, here, instead of
 * being remembered at each call site by whoever writes the next screen.
 *
 * The second is [enabled], which is not the same thing as calling this only
 * when back matters. **A disabled handler is not an absent handler.** A press
 * is offered to the most recently registered *enabled* callback and to no
 * other, so a screen that declares a disabled handler lets the press fall
 * through to whatever registered before it — the navigation host, and past that
 * the activity. Screen SCR-101 keeps this handler enabled always, so the host
 * never takes a press on the browsing surface: page history first, then the
 * launcher, never chrome. Wrapping the call in `if (…)` instead would look
 * equivalent and behave the same, and only because Compose removes the
 * callback when the call leaves the composition; spelling it as [enabled]
 * says the sharing is intended rather than incidental.
 */
@Composable
fun TaffyBackHandler(enabled: Boolean, onBack: () -> Unit) {
    if (LocalOnBackPressedDispatcherOwner.current != null) {
        BackHandler(enabled = enabled, onBack = onBack)
    }
}
