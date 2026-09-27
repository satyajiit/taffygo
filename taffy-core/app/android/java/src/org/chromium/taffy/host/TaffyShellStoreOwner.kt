// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.lifecycle.HasDefaultViewModelProviderFactory
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.viewmodel.CreationExtras
import java.io.Closeable

/**
 * One product window's view-model store, with TaffyGo's factory in front of it.
 *
 * This class is the seam between the platform-independent screens and the
 * profile/window Dagger graph. `screenViewModel(destination)` in `:core:ui` resolves through
 * `LocalViewModelStoreOwner.current as? HasDefaultViewModelProviderFactory`, and
 * takes that host's `defaultViewModelProviderFactory`. Chromium's activity
 * otherwise supplies a reflection-based
 * `SavedStateViewModelFactory`, which builds a view model by reflection over a
 * no-argument or `(SavedStateHandle)` constructor, and every screen's view model
 * here takes repositories.
 *
 * THE STORE IS THE WINDOW'S, NOT THE ACTIVITY'S. Every view model this owner
 * holds was built from one window component's factory, so it holds that
 * window's repositories and, through them, its `ChromiumBrowserMediator`. The
 * window component is closed in `TaffyBrowserActivity.onDestroy` and the
 * mediator is destroyed a few lines later. The activity's own store is not
 * closed there: Android keeps it across a configuration-change recreation, and
 * upstream's `ChromeBaseAppCompatActivity.onNightModeStateChanged` recreates the
 * activity every time the system switches between light and dark. Keeping these
 * view models in that store handed the new window the old window's view models:
 * their collectors kept running against a destroyed mediator, and the first
 * frequent-sites update after a light/dark switch closed the browser with "A
 * destroyed browser mediator cannot act". So the store is created here, one per
 * window, and [close] clears it when the window lifetime ends — before the
 * mediator it reaches is destroyed. A recreated activity builds its view models
 * again from its own window's factory.
 *
 * What a screen keeps across that rebuild is exactly what it keeps across
 * process death, by the same path. The creation extras stay the activity's, so
 * `createSavedStateHandle()` still files the shell's handle — the back stack —
 * in the activity's saved-state registry, and each destination entry saves its
 * own state through that registry too (`DestinationStateStore` in `:core:ui`).
 *
 * Nothing here is scoped per screen. `DestinationViewModelStoreOwner` in
 * `:core:ui` does that, wrapping this one per destination, and it is the same
 * class in both builds.
 */
class TaffyShellStoreOwner(
    private val defaults: HasDefaultViewModelProviderFactory,
    private val factory: ViewModelProvider.Factory,
) : ViewModelStoreOwner, HasDefaultViewModelProviderFactory, Closeable {

    override val viewModelStore: ViewModelStore = ViewModelStore()

    override val defaultViewModelProviderFactory: ViewModelProvider.Factory
        get() = factory

    override val defaultViewModelCreationExtras: CreationExtras
        get() = defaults.defaultViewModelCreationExtras

    /** Clears every view model the window built; their scopes are cancelled with them. */
    override fun close() {
        viewModelStore.clear()
    }
}
