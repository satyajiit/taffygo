// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.BackStack
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import java.util.concurrent.atomic.AtomicReference
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * The shell's one source of truth: the back stack and the theme.
 *
 * The back stack is kept as routes in the saved-state handle, so it survives
 * process death, and it is rebuilt through
 * [TaffyDestination.fromRoute] — restoring can only produce destinations the
 * application actually has.
 */
class ShellViewModel @Inject constructor(
    private val preferences: UserPreferencesRepository,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val restored: Boolean = !savedState.get<ArrayList<String>>(BACK_STACK_KEY).isNullOrEmpty()

    private val backStack = MutableStateFlow(restoreBackStack())

    /**
     * How the browsing surface puts the window behind the last app.
     *
     * The view model has no activity. The composition that does binds one
     * here for the life of the tree, and unbinds it on dispose, so a
     * destroyed activity is never asked to move itself.
     */
    private val onLeaveToBackground = AtomicReference<() -> Boolean>({ false })

    /** The navigation contract's implementation, handed to every screen. */
    val navigator: TaffyNavigator = ShellNavigator(
        current = { backStack.value },
        update = { stack ->
            backStack.value = stack
            savedState[BACK_STACK_KEY] = ArrayList(stack.entries.map { it.route })
        },
        onLeaveToBackground = { onLeaveToBackground.get().invoke() },
    )

    /** Point [TaffyNavigator.leaveToBackground] at this window, or at nothing. */
    fun bindLeaveToBackground(action: () -> Boolean) {
        onLeaveToBackground.set(action)
    }

    /** What the shell renders. */
    val state: StateFlow<ShellUiState> =
        combine(backStack, preferences.preferences) { stack, settings ->
            ShellUiState(
                backStack = stack,
                theme = settings.theme,
                appLanguage = settings.appLanguage,
                regionCode = settings.regionCode,
                preferencesLoaded = settings.loaded,
                pseudoLocalization = settings.pseudoLocalization,
                forceDarkWeb = settings.forceDarkWeb,
            )
        }.stateIn(
            scope = viewModelScope,
            started = SharingStarted.Eagerly,
            initialValue = ShellUiState(backStack = backStack.value),
        )

    init {
        // Where the UI layer opens is a question about stored state, so it is
        // asked once the stored state has arrived and only when there is no
        // saved stack to honour: a process death mid-sequence restores where
        // the user was, and a device that has finished the sequence opens the
        // browser. Nothing renders until `preferencesLoaded`, so this cannot
        // move the ground under a screen the user is already on.
        viewModelScope.launch {
            val settings = preferences.preferences.first { it.loaded }
            if (!settings.onboardingCompleted && !restored) {
                backStack.value = BackStack.start(TaffyDestination.OnboardingWelcome)
            }
        }
    }

    private fun restoreBackStack(): BackStack {
        val routes = savedState.get<ArrayList<String>>(BACK_STACK_KEY).orEmpty()
        val entries = routes.mapNotNull(TaffyDestination::fromRoute)
        return if (entries.isEmpty()) BackStack.start() else BackStack(entries)
    }

    private companion object {
        const val BACK_STACK_KEY = "shell_back_stack"
    }
}
