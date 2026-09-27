// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.task.TaskInputRepository
import com.taffygo.browser.ui.core.task.TaskRepository
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * What the browsing surface has to say about a task working in it.
 *
 * A second view model on screen SCR-101 rather than three more flows on
 * [BrowserMainViewModel], because it is a different subject with a different
 * lifetime: everything the main one holds is true of the page, and everything
 * here is true of a task that may have been started on another screen
 * altogether. Keeping them apart also keeps the main projection's signature —
 * and every host test that calls it — untouched by a task.
 */
class BrowserTakeoverViewModel(
    private val tasks: TaskRepository,
    forms: TaskInputRepository,
    browser: BrowserRepository,
) : ViewModel() {

    /** What the frame, the band and the cut-out read. */
    val state: StateFlow<TakeoverUiState> = combine(
        tasks.state,
        forms.request,
        browser.navigation,
    ) { repository, request, navigation ->
        projectTakeover(repository, request, navigation.host)
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = projectTakeover(
            tasks.state.value,
            forms.request.value,
            browser.navigation.value.host,
        ),
    )

    /** Give the page back without ending the task. */
    fun takeOver() {
        val rendered = state.value
        val taskId = rendered.taskId ?: return
        val taskRevision = rendered.taskRevision ?: return
        if (!rendered.canTakeOver) return
        viewModelScope.launch {
            tasks.useControl(taskId, taskRevision, TaskControl.TAKE_OVER)
        }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}
