// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import com.taffygo.browser.ui.core.browser.ErrandPagePort
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.SharingStarted
import androidx.lifecycle.viewModelScope

/**
 * Screen SCR-110's one source of truth.
 *
 * It holds no page. The page belongs to the browser, and this reads the one
 * record the browser publishes about it and turns two intents back into two
 * calls. That is the whole of it, deliberately: an errand page with state of
 * its own would be a second opinion about a page whose only real state is in
 * the engine.
 *
 * ## Why the identity comes from the saved-state handle
 *
 * The route carries it, so it survives process death, so this view model is
 * asking about the same errand after a restart that it was asking about
 * before — and gets the honest answer that the page is gone. Reading it from
 * the handle rather than taking it as a constructor argument is what makes
 * that true without this class knowing anything about restoration.
 */
class ErrandPageViewModel(
    private val errand: ErrandPagePort,
    savedState: SavedStateHandle,
) : ViewModel() {

    private val errandId: String =
        savedState.get<String>(TaffyDestination.ERRAND_ID).orEmpty()

    /** What screen SCR-110 renders. */
    val state: StateFlow<ErrandPageUiState> = errand.page
        .map { page -> ErrandPageReducer.project(page, errandId) }
        .stateIn(
            viewModelScope,
            SharingStarted.WhileSubscribed(STOP_TIMEOUT_MILLIS),
            ErrandPageReducer.project(errand.page.value, errandId),
        )

    fun onIntent(intent: ErrandPageIntent, navigator: TaffyNavigator) {
        when (intent) {
            // The page's own history first, and the errand only when there is
            // none left. `goBack` answers whether it moved, which is exactly
            // the question, so nothing here has to read `canGoBack` and race
            // the engine's own answer to it.
            ErrandPageIntent.Back -> if (!errand.goBack(errandId)) navigator.goBack()
            ErrandPageIntent.Close -> navigator.goBack()
        }
    }

    /**
     * Ends the errand.
     *
     * Called from the screen's disposal rather than from [onCleared], and the
     * difference is load-bearing: a view model is cleared after its scope is
     * cancelled, so work started there would never run, while a composition
     * being disposed is exactly the moment the errand stopped being on screen.
     * It is also the moment that covers the cases nothing else does — an
     * inbound link replacing the whole back stack, a person leaving by any
     * route at all.
     */
    fun endErrand() {
        errand.close(errandId)
    }

    private companion object {
        const val STOP_TIMEOUT_MILLIS = 5_000L
    }
}
