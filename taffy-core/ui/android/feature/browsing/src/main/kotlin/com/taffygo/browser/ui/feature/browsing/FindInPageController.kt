// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch

/**
 * Owns find in page (SCR-106) independently of SCR-101.
 *
 * The overlay state, the one in-flight engine request and the debounce that
 * keeps a person's typing from becoming a search per keystroke all live here.
 * Every engine answer is checked against the request generation that asked
 * for it, so a superseded search never writes its count over a newer phrase,
 * and closing cancels whatever was still waiting.
 */
internal class FindInPageController(
    private val port: FindInPagePort,
    private val scope: CoroutineScope,
) {
    private val mutableState = MutableStateFlow(FindInPageUiState())
    val state: StateFlow<FindInPageUiState> = mutableState.asStateFlow()
    private var requestGeneration = 0L
    private var job: Job? = null

    init {
        port.matches
            .onEach { matches ->
                val current = mutableState.value
                if (current.open && current.query.isNotBlank()) {
                    mutableState.value = current.copy(
                        activeIndex = matches.activeIndex,
                        matchCount = matches.total,
                    )
                }
            }
            .launchIn(scope)
    }

    /** Draw the overlay over the current page, with an empty field. */
    fun open() {
        mutableState.value = openFindInPage(port.isAvailable)
    }

    /** Close the overlay, forget the phrase, and stop any search still waiting. */
    fun close() {
        requestGeneration += 1
        job?.cancel()
        job = null
        scope.launch { port.clear() }
        mutableState.value = reduceFindInPage(mutableState.value, FindInPageIntent.Close)
    }

    /** Act on one overlay intent; only the newest request's answer is shown. */
    fun apply(intent: FindInPageIntent) {
        val generation = ++requestGeneration
        job?.cancel()
        if (intent is FindInPageIntent.QueryChanged) {
            mutableState.value = reduceFindInPage(
                mutableState.value,
                intent,
                activeIndex = 0,
                matchCount = 0,
            )
        }
        job = scope.launch {
            val matches = when (intent) {
                is FindInPageIntent.QueryChanged ->
                    if (intent.query.isBlank()) {
                        port.clear()
                        FindInPagePort.MatchCount()
                    } else {
                        delay(QUERY_DEBOUNCE_MILLIS)
                        port.find(intent.query)
                    }
                FindInPageIntent.Next -> port.next()
                FindInPageIntent.Previous -> port.previous()
                FindInPageIntent.Close -> FindInPagePort.MatchCount()
            }
            if (generation != requestGeneration || !mutableState.value.open) return@launch
            if (
                intent is FindInPageIntent.QueryChanged &&
                mutableState.value.query != intent.query
            ) {
                return@launch
            }
            mutableState.value = reduceFindInPage(
                mutableState.value,
                intent,
                matches.activeIndex,
                matches.total,
            )
        }
    }

    companion object {
        /** How long typing has to pause before the engine is asked. */
        internal const val QUERY_DEBOUNCE_MILLIS = 100L
    }
}
