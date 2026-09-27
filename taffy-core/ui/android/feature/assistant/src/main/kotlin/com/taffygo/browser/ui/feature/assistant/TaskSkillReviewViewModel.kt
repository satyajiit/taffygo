// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import com.taffygo.browser.ui.core.api.acceptanceMutation
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.api.toSavedFlowReview
import com.taffygo.browser.ui.core.model.SavedFlowReview
import com.taffygo.browser.ui.core.task.TaskRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import taffy.core_api.SiteSkillStatusView
import taffy.core_api.SiteSkillView
import taffy.core_api.TaskPhase

/** Offers only the recording the core attributed to the completed task currently on screen. */
class TaskSkillReviewViewModel(
    private val core: CoreApiClient,
    private val tasks: TaskRepository,
    private val reviews: SavedFlowReviewRepository? = null,
) : ViewModel() {
    private val interaction = MutableStateFlow(Interaction())
    val state: StateFlow<TaskSkillReviewUiState> = combine(
        tasks.state.map { it.task?.let { task -> task.id to task.phase } }.distinctUntilChanged(),
        (reviews?.status ?: core.status).map { it.takeIf { status -> status.hasCompleteProjection() }?.site_skills }
            .distinctUntilChanged(),
        interaction,
    ) { task, skills, action ->
        project(task, skills.orEmpty(), action)
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000L), TaskSkillReviewUiState())

    fun reviewCurrent() {
        val shown = state.value
        shown.review?.let { review(it); return }
        val id = shown.skillId ?: return
        if (shown.loading) return
        val key = id to shown.version
        interaction.value = Interaction(loading = key)
        viewModelScope.launch {
            val loaded = reviews?.load(id, shown.version) == true
            if (interaction.value.loading != key || tasks.state.value.task?.id != shown.taskId) return@launch
            interaction.value = if (loaded) Interaction(reviewing = key) else Interaction(failed = key)
        }
    }

    fun review(flow: SavedFlowReview) {
        if (currentDraft(flow) == null) return
        interaction.value = Interaction(reviewing = flow.id to flow.version)
    }

    fun closeReview() {
        if (interaction.value.submitting == null) interaction.value = Interaction()
    }

    fun dismissCurrent() {
        val shown = state.value
        val id = shown.skillId ?: shown.review?.id ?: return
        if (interaction.value.submitting == null) {
            interaction.value = Interaction(dismissed = id to (shown.review?.version ?: shown.version))
        }
    }

    fun dismiss(flow: SavedFlowReview) {
        if (interaction.value.submitting == null) {
            interaction.value = Interaction(dismissed = flow.id to flow.version)
        }
    }

    fun accept(flow: SavedFlowReview) {
        val key = flow.id to flow.version
        if (interaction.value.reviewing != key || interaction.value.submitting != null) return
        if (currentDraft(flow) == null) {
            interaction.value = Interaction(reviewing = key, failed = key)
            return
        }
        interaction.value = Interaction(reviewing = key, submitting = key)
        viewModelScope.launch {
            try {
                core.mutateSiteSkill(flow.acceptanceMutation())
            } catch (_: CoreApiSubmissionException) {
                interaction.value = Interaction(reviewing = key, failed = key)
            }
        }
    }

    private fun currentDraft(flow: SavedFlowReview): SiteSkillView? {
        val task = tasks.state.value.task ?: return null
        if (task.phase != TaskPhase.COMPLETED || !core.status.value.hasCompleteProjection()) return null
        return (reviews?.current() ?: core.status.value).site_skills.singleOrNull {
            it.recorded_from_task_id == task.id && it.status == SiteSkillStatusView.DRAFT &&
                it.toSavedFlowReview() == flow
        }
    }

    private fun project(
        task: Pair<String, TaskPhase>?,
        skills: List<SiteSkillView>,
        action: Interaction,
    ): TaskSkillReviewUiState {
        if (task == null || task.second != TaskPhase.COMPLETED) return TaskSkillReviewUiState()
        val skill = skills.singleOrNull {
            it.recorded_from_task_id == task.first &&
                it.status in listOf(SiteSkillStatusView.DRAFT, SiteSkillStatusView.ACTIVE)
        } ?: return TaskSkillReviewUiState()
        val flow = skill.toSavedFlowReview()
        val key = skill.skill_id to skill.active_version
        if (action.dismissed == key) return TaskSkillReviewUiState()
        val saved = skill.status == SiteSkillStatusView.ACTIVE
        return TaskSkillReviewUiState(
            taskId = task.first,
            skillId = skill.skill_id, version = skill.active_version, origin = skill.origin,
            loading = action.loading == key,
            review = flow,
            saved = saved,
            submitting = !saved && action.submitting == key,
            showReview = !saved && action.reviewing == key,
            failed = !saved && action.failed == key,
        )
    }

    private data class Interaction(
        val reviewing: Pair<String, UInt>? = null,
        val loading: Pair<String, UInt>? = null,
        val submitting: Pair<String, UInt>? = null,
        val dismissed: Pair<String, UInt>? = null,
        val failed: Pair<String, UInt>? = null,
    )
}
