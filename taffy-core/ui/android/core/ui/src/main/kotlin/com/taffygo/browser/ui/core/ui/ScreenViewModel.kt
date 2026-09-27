// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.lifecycle.HasDefaultViewModelProviderFactory
import androidx.lifecycle.SAVED_STATE_REGISTRY_OWNER_KEY
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.core.ui.internal.DestinationStateStore
import com.taffygo.browser.ui.core.ui.internal.DestinationViewModelStoreOwner

/**
 * The screen's view model, scoped to the destination showing it.
 *
 * Each destination gets its own store, so two screens never share an instance
 * and a screen's view model is cleared when its destination leaves the back
 * stack. The factory is the window component's Dagger factory. Each feature
 * contributes its own creator, so this shared code contains no class switch.
 *
 * The saved state is namespaced by the destination's route, so two screens can
 * both keep a `query` without overwriting each other after process death.
 */
@Composable
inline fun <reified VM : ViewModel> screenViewModel(destination: TaffyDestination): VM =
    viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(destination),
        key = destination.route,
    )

/**
 * The back-stack entry for one destination: its view models and its saved
 * state, for as long as the back stack holds it.
 *
 * The entry outlives the composition. A screen navigated away from and returned
 * to is the same entry, holding what was typed into it, and a screen the stack
 * has left behind is released by [ReleasePoppedDestinationState] — which is
 * what "cleared when its destination leaves the back stack" above has always
 * meant and now does.
 *
 * A window with no store behind it — a preview, a screen test — gets an entry
 * of its own that ends with the composition, which is the whole of its life
 * there.
 */
@Composable
fun rememberScreenViewModelStoreOwner(
    destination: TaffyDestination,
    key: String = destination.route,
): ViewModelStoreOwner {
    val host = checkNotNull(LocalViewModelStoreOwner.current as? HasDefaultViewModelProviderFactory) {
        "No host with a default view-model factory: attach the tree to a Taffy window."
    }
    val store = rememberDestinationStateStore()
    val owner = remember(destination.route, key, store) {
        val entry = store?.entryFor(host, destination) ?: DestinationViewModelStoreOwner(host)
        // Reading the handle is what puts the destination's arguments in it,
        // and it has to happen before the view model is built, because the view
        // model reads them in its first line.
        entry.savedStateFor(destination, key)
        entry
    }
    // The entry is told when its screen is on the glass and when it is gone,
    // because "left the back stack" and "no longer drawn" are a transition
    // apart and an entry ended in between is an entry that comes back. A window
    // with no store keeps nothing after the screen, so its entry ends here.
    DisposableEffect(owner) {
        owner.onComposed()
        onDispose {
            owner.onDisposed()
            if (store == null) owner.destroy()
        }
    }
    return owner
}

/**
 * Release every destination the back stack has left behind.
 *
 * The navigation host composes this beside the screen it draws, and it is the
 * whole of the pruning rule: `BackStack.popped` already answers "what did this
 * navigation leave behind", for a pop, a replacement, a return home and a whole
 * group left at once alike, and what it names is what is released. Nothing here
 * is on a clock and nothing here guesses — a destination's state ends when its
 * destination does.
 *
 * A window that keeps no store has nothing to release, and this does nothing.
 */
@Composable
fun ReleasePoppedDestinationState(backStack: BackStack) {
    val store = rememberDestinationStateStore()
    val previous = remember { mutableStateOf(backStack) }
    LaunchedEffect(backStack) {
        val left = backStack.popped(previous.value)
        previous.value = backStack
        store?.release(left.map { it.route })
    }
}

/**
 * The window's destination-state store, or null when there is no window.
 *
 * Found through the same owner every screen's view model is found through, so
 * the store lives exactly as long as those do. The registry it saves through is
 * named by that owner's own creation extras, because that is where
 * `createSavedStateHandle()` has always read it from and a second way to name
 * one window is a second thing that can disagree.
 */
@Composable
private fun rememberDestinationStateStore(): DestinationStateStore? {
    val owner = LocalViewModelStoreOwner.current
    val registryOwner = (owner as? HasDefaultViewModelProviderFactory)
        ?.defaultViewModelCreationExtras
        ?.get(SAVED_STATE_REGISTRY_OWNER_KEY)
    return remember(owner, registryOwner) {
        owner?.let(DestinationStateStore::of)?.also { store ->
            registryOwner?.let(store::attachTo)
        }
    }
}
