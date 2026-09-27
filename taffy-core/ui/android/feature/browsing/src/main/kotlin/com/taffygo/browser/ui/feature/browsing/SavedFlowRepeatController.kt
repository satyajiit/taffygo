// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.api.toSavedFlowReview
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.SavedFlowReview
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch
import taffy.core_api.SiteSkillStatusView

/** One composer's explicit submissions; typing, tab switches and new generations withdraw replies. */
internal class SavedFlowRepeatController(
    private val browser: BrowserRepository,
    private val repository: SavedFlowReviewRepository?,
    private val pages: SavedFlowPageRequests?,
    private val scope: CoroutineScope,
    private val onChange: (SavedFlowRepeatState) -> Unit = {},
) {
    val state = MutableStateFlow(SavedFlowRepeatState())
    private var request = 0L
    private var work: Job? = null
    private var goal = ""
    private var selected = browser.tabs.value.singleOrNull { it.isSelected }?.id
    private var address = browser.navigation.value.canonicalUrl

    init {
        if (repository != null) scope.launch {
            combine(browser.tabs, browser.navigation, repository.status) { tabs, navigation, status ->
                Triple(tabs, navigation, status)
            }.collect { (tabs, navigation, status) ->
                val current = tabs.singleOrNull { it.isSelected }
                val reviews = state.value.reviews
                if (current?.id != selected || current?.isPrivate == true || current?.isTaffyTab == true ||
                    (!state.value.opening && navigation.canonicalUrl != address) ||
                    (!status.hasCompleteProjection() && state.value.visible) ||
                    reviews.any { review -> status.site_skills.none {
                        it.status == SiteSkillStatusView.ACTIVE && it.toSavedFlowReview() == review
                    } }
                ) clear()
            }
        }
    }

    fun find(submitted: String, noMatch: () -> Unit): Boolean {
        val repo = repository ?: return false
        val tab = browser.tabs.value.singleOrNull { it.isSelected } ?: return false
        if (tab.isPrivate || tab.isTaffyTab) return false
        clear()
        selected = tab.id
        address = browser.navigation.value.canonicalUrl
        goal = submitted
        val ticket = request
        publish(SavedFlowRepeatState(checking = true))
        work = scope.launch {
            val found = repo.find(submitted)
            if (ticket != request) return@launch
            publish(when {
                found == null -> SavedFlowRepeatState(failed = true)
                found.isEmpty() -> SavedFlowRepeatState()
                else -> SavedFlowRepeatState(reviews = found)
            })
            if (found?.isEmpty() == true) noMatch()
        }
        return true
    }

    fun open(review: SavedFlowReview, navigator: TaffyNavigator) {
        if (state.value.opening || review !in state.value.reviews) return
        val ticket = request
        publish(state.value.copy(opening = true, failed = false))
        work = scope.launch {
            val opened = repository?.open(review) == true
            if (ticket != request) return@launch
            if (opened) {
                selected?.let { pages?.show(goal, review, it) }
                clear()
                navigator.replaceCurrent(TaffyDestination.BrowserMain)
            } else publish(state.value.copy(opening = false, failed = true))
        }
    }

    private fun publish(value: SavedFlowRepeatState) {
        state.value = value
        onChange(value)
    }

    fun clear() {
        request++
        work?.cancel()
        work = null
        goal = ""
        publish(SavedFlowRepeatState())
    }
}

internal fun AddressBarIntent.withdrawsSavedFlowReview(): Boolean =
    this is AddressBarIntent.InputChanged || this == AddressBarIntent.Left ||
        this == AddressBarIntent.Dismiss || this is AddressBarIntent.ChooseShape ||
        this == AddressBarIntent.ClearShape || this is AddressBarIntent.ToggleStore ||
        this is AddressBarIntent.ConfirmAttachPages || this is AddressBarIntent.RemovePage

internal fun AddressBarUiState.canFindSavedFlow(chosen: com.taffygo.browser.ui.core.model.AddressBarInterpretation): Boolean =
    !starting && chosen == reading && attachedStores.isEmpty() && !conditions.taskAlreadyRunning &&
        (chosen is com.taffygo.browser.ui.core.model.AddressBarInterpretation.AskTaffy ||
            (chosen as? com.taffygo.browser.ui.core.model.AddressBarInterpretation.TaskForTaffy)?.template ==
                com.taffygo.browser.ui.core.model.TaskTemplate.WEB_ERRAND)
