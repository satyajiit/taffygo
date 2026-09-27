// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui.internal

import android.os.Bundle
import androidx.lifecycle.HasDefaultViewModelProviderFactory
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.SAVED_STATE_REGISTRY_OWNER_KEY
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.VIEW_MODEL_STORE_OWNER_KEY
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.createSavedStateHandle
import androidx.lifecycle.enableSavedStateHandles
import androidx.lifecycle.viewmodel.CreationExtras
import androidx.lifecycle.viewmodel.MutableCreationExtras
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import androidx.savedstate.SavedStateRegistry
import androidx.savedstate.SavedStateRegistryController
import androidx.savedstate.SavedStateRegistryOwner
import com.taffygo.browser.ui.core.ui.TaffyDestination

/**
 * One entry of the back stack: a destination's view models and its saved state.
 *
 * The host is the application shell's activity, whose default factory comes
 * from its Window component. That factory is all this takes from the host.
 * Instances, saved state and lifetime belong to the destination, because that
 * is whose they are: two screens never share a view model, and what is typed
 * into one screen is not what the next one opens holding.
 *
 * ## Why the entry owns a saved-state registry of its own
 *
 * `createSavedStateHandle()` files a handle under the key it was asked for, in
 * whichever store and registry the creation extras name. Naming the activity in
 * both — which is what this class used to do — files every screen's handle in
 * one activity-wide map that nothing ever takes an entry out of. Every route a
 * person visits leaves one behind for the life of the window: fifty workspaces
 * opened is fifty handles, and the since-retired task preview's key was the
 * goal in the person's own words, so what accumulated was what they had typed, under its
 * own name, until the window closed.
 *
 * Naming *this* entry in both puts the handle where its lifetime already is.
 * [destroy] ends it, and [DestinationStateStore] calls that when the back stack
 * leaves the destination behind. Nothing else changes: the platform still
 * creates the handle, still restores it, and still saves it — through [save]
 * here rather than through the activity's own — so a screen still comes back
 * from process death holding what was typed into it.
 *
 * The lifecycle exists for the same reason. `enableSavedStateHandles` is how a
 * saved-state registry owner that is not an activity is allowed to hand out
 * handles at all, and it attaches on the entry's own `ON_CREATE`.
 *
 * [lifecycleOf] is that lifecycle, and it is a parameter for one reason: a
 * `LifecycleRegistry` asks the platform whether it is on the main thread before
 * it accepts an observer, and a host test has no `Looper` to answer with. The
 * default is the real one, so nothing a build reaches is affected; a host test
 * passes the registry androidx publishes for exactly this and gets to assert on
 * a real entry rather than on a stand-in for one.
 */
internal class DestinationViewModelStoreOwner(
    private val host: HasDefaultViewModelProviderFactory,
    savedState: Bundle? = null,
    lifecycleOf: (LifecycleOwner) -> LifecycleRegistry = ::LifecycleRegistry,
) : ViewModelStoreOwner, HasDefaultViewModelProviderFactory, SavedStateRegistryOwner {

    override val viewModelStore: ViewModelStore = ViewModelStore()

    private val lifecycleRegistry = lifecycleOf(this)

    private val controller = SavedStateRegistryController.create(this)

    override val lifecycle: Lifecycle
        get() = lifecycleRegistry

    override val savedStateRegistry: SavedStateRegistry
        get() = controller.savedStateRegistry

    private var drawn = 0

    private var released = false

    private var destroyed = false

    init {
        // The platform's own order, and each step checks the one before it:
        // restore while the entry is still uninitialised, wire the handles in
        // before anything can ask for one, and only then open the lifecycle so
        // the attacher's `ON_CREATE` is the first event it sees.
        controller.performRestore(savedState)
        enableSavedStateHandles()
        lifecycleRegistry.currentState = Lifecycle.State.CREATED
    }

    override val defaultViewModelProviderFactory: ViewModelProvider.Factory
        get() = host.defaultViewModelProviderFactory

    override val defaultViewModelCreationExtras: CreationExtras
        get() = MutableCreationExtras(host.defaultViewModelCreationExtras).apply {
            set(SAVED_STATE_REGISTRY_OWNER_KEY, this@DestinationViewModelStoreOwner)
            set(VIEW_MODEL_STORE_OWNER_KEY, this@DestinationViewModelStoreOwner)
        }

    /**
     * The handle the screen's view model will be built with, holding
     * [destination]'s arguments.
     *
     * Asked for under the view model's exact [key], ordinarily the destination's
     * route. A second view model on one entry keeps its own key and handle,
     * while receiving the same destination arguments before construction.
     *
     * An argument is written only where the handle does not already answer for
     * it. A handle that has been restored, or that a screen has already edited,
     * holds what that screen last had — and an argument written over the top of
     * it would undo the edit and reopen the screen on what it was opened with a
     * week ago.
     */
    fun savedStateFor(
        destination: TaffyDestination,
        key: String = destination.route,
    ): SavedStateHandle {
        val handle = MutableCreationExtras().apply {
            set(SAVED_STATE_REGISTRY_OWNER_KEY, this@DestinationViewModelStoreOwner)
            set(VIEW_MODEL_STORE_OWNER_KEY, this@DestinationViewModelStoreOwner)
            set(ViewModelProvider.VIEW_MODEL_KEY, key)
        }.createSavedStateHandle()
        for ((name, value) in destination.arguments) {
            if (!handle.contains(name)) handle[name] = value
        }
        return handle
    }

    /** This entry's saved state, as the window's own registry asks for it. */
    fun save(): Bundle = Bundle().also(controller::performSave)

    /** The composition has begun drawing this entry's screen. */
    fun onComposed() {
        drawn += 1
    }

    /** The composition has finished with this entry's screen. */
    fun onDisposed() {
        drawn -= 1
        if (released && drawn <= 0) destroy()
    }

    /**
     * The back stack has left this entry behind.
     *
     * It ends here unless its screen is still on the glass, and one is for as
     * long as the navigation transition runs — a couple of hundred milliseconds
     * in which the leaving screen is still composed and still recomposing. An
     * entry ended under a screen that is still being drawn does not stay ended:
     * the next recomposition asks for the view model, finds the store empty and
     * builds another one, on a scope now attached to nothing that will ever
     * clear it. So a released entry that is still drawn is ended by [onDisposed]
     * instead, which is the moment its screen is actually gone.
     */
    fun release() {
        released = true
        if (drawn <= 0) destroy()
    }

    /** End the entry: its view models are cleared and its state is gone. */
    fun destroy() {
        if (destroyed) return
        destroyed = true
        lifecycleRegistry.currentState = Lifecycle.State.DESTROYED
        viewModelStore.clear()
    }
}

/**
 * Every destination's view models and saved state, for exactly as long as the
 * back stack holds that destination.
 *
 * One entry per route, created when the destination is first drawn and released
 * when the shell reports the destination gone. Nothing here is on a clock and
 * nothing here guesses: `BackStack.popped` already answers "what did this
 * navigation leave behind" — for a pop, a replacement, a return home and a
 * whole group left at once alike — and what it names is what is released.
 *
 * Retaining the entries here rather than in the composition is also what makes
 * `screenViewModel`'s own promise true. It has always said a screen's view
 * model is cleared "when its destination leaves the back stack", while the
 * owner it built was remembered by the composition and cleared whenever the
 * screen stopped being drawn — so a screen navigated away from and returned to
 * was rebuilt from nothing.
 *
 * The store is a view model in the window's store, so it survives what the
 * window survives — a rotation, a size change — and is cleared with it. Process
 * death is the registry's half: each live entry saves its own state under its
 * route, and state belonging to a route that has not been drawn yet is carried
 * through untouched rather than dropped, because a restored back stack is
 * walked one screen at a time.
 */
internal class DestinationStateStore(
    private val lifecycleOf: (LifecycleOwner) -> LifecycleRegistry = ::LifecycleRegistry,
) : ViewModel(), SavedStateRegistry.SavedStateProvider {

    private val entries = LinkedHashMap<String, DestinationViewModelStoreOwner>()

    private var restored: Bundle? = null

    /** The routes the store is holding an entry for. */
    val routes: Set<String>
        get() = entries.keys.toSet()

    /**
     * Register with [owner]'s saved-state registry, and take what it restored.
     *
     * Called on every composition and does its work once per window. A store
     * that already holds entries has outlived a window rather than a process —
     * a rotation — and its entries are the truth; the bundle the new activity
     * restored is the same state one save older, so it is left where it is.
     */
    fun attachTo(owner: SavedStateRegistryOwner) {
        val registry = owner.savedStateRegistry
        if (registry.getSavedStateProvider(PROVIDER_KEY) === this) return
        if (restored == null && entries.isEmpty() && registry.isRestored) {
            restored = registry.consumeRestoredStateForKey(PROVIDER_KEY)
        }
        registry.registerSavedStateProvider(PROVIDER_KEY, this)
    }

    /** [destination]'s entry: the one it already has, or a new one. */
    fun entryFor(
        host: HasDefaultViewModelProviderFactory,
        destination: TaffyDestination,
    ): DestinationViewModelStoreOwner =
        entries.getOrPut(destination.route) {
            DestinationViewModelStoreOwner(host, consumeRestored(destination.route), lifecycleOf)
        }

    /**
     * Forget [routes] entirely: their view models, their saved state, and
     * anything restored for them that was never asked for.
     *
     * A destination opened again is opened as a new screen, which is what
     * leaving one means.
     */
    fun release(routes: Collection<String>) {
        for (route in routes) {
            entries.remove(route)?.release()
            restored?.remove(route)
        }
    }

    override fun saveState(): Bundle {
        val saved = Bundle()
        restored?.let(saved::putAll)
        for ((route, entry) in entries) {
            saved.putBundle(route, entry.save())
        }
        return saved
    }

    override fun onCleared() {
        for (entry in entries.values) entry.destroy()
        entries.clear()
        restored = null
    }

    private fun consumeRestored(route: String): Bundle? {
        val state = restored ?: return null
        return state.getBundle(route)?.also { state.remove(route) }
    }

    companion object {
        /** This store's one key in the window's saved-state registry. */
        const val PROVIDER_KEY: String = "com.taffygo.browser.ui.destination-state"

        /**
         * How the store is built, once per window.
         *
         * Not the window's own factory: that one is Dagger's, and it answers
         * for the screens' view models rather than for this.
         */
        private val FACTORY: ViewModelProvider.Factory = viewModelFactory {
            initializer { DestinationStateStore() }
        }

        /**
         * The window's one destination-state store, created on first use.
         *
         * The window's view-model store is both where it is anchored and where
         * it is found again: it outlives every composition inside the window
         * and is cleared when the window is finished, which is exactly the
         * lifetime this state has.
         */
        fun of(owner: ViewModelStoreOwner): DestinationStateStore =
            ViewModelProvider.create(owner.viewModelStore, FACTORY)[DestinationStateStore::class]
    }
}
