// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.task.TaskRepository
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.mapLatest
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import taffy.core_api.TaskPhase

/** File completion comes from the browser; task completion alone never invents a file. */
class TaskDownloadsViewModel(
    private val browser: BrowserRepository,
    private val tasks: TaskRepository,
) : ViewModel() {
    private val interaction = MutableStateFlow(TaskDownloadsUiState())
    @OptIn(ExperimentalCoroutinesApi::class)
    private val files = combine(
        tasks.state.map { state -> state.task?.takeIf { it.phase.canHaveResult() }?.let { it.id to it.revision } }
            .distinctUntilChanged(),
        browser.downloads,
    ) { task, _ -> task }.mapLatest { task ->
        if (task == null) TaskDownloadsUiState() else TaskDownloadsUiState(
            taskId = task.first,
            files = browser.completedTaskDownloads(task.first).filter {
                it.state == DownloadState.COMPLETE && DownloadAction.OPEN in it.allowedActions
            }.distinctBy { it.id },
        )
    }

    val state: StateFlow<TaskDownloadsUiState> = combine(files, interaction) { snapshot, action ->
        if (snapshot.taskId != action.taskId) snapshot else snapshot.copy(
            opening = action.opening?.takeIf { id -> snapshot.files.any { it.id == id } },
            failed = action.failed?.takeIf { id -> snapshot.files.any { it.id == id } },
        )
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000L), TaskDownloadsUiState())

    /** Only an explicit click reaches the browser's fresh ownership and file-state check. */
    fun open(taskId: String?, id: DownloadId) {
        val task = tasks.state.value.task ?: return
        if (taskId == null || task.id != taskId || !task.phase.canHaveResult() ||
            state.value.taskId != taskId || state.value.files.none { it.id == id } ||
            (interaction.value.taskId == taskId && interaction.value.opening != null)
        ) return
        val pending = TaskDownloadsUiState(taskId = taskId, opening = id)
        interaction.value = pending
        viewModelScope.launch {
            val opened = browser.openTaskDownload(taskId, id)
            if (interaction.value == pending) interaction.value = TaskDownloadsUiState(
                taskId = taskId, failed = id.takeUnless { opened },
            )
        }
    }
}

private fun TaskPhase.canHaveResult(): Boolean =
    this == TaskPhase.COMPLETED || this == TaskPhase.PARTIAL || this == TaskPhase.FAILED
