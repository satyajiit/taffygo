// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.model.SavedFlowReview
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Screen SCR-602's one source of truth.
 *
 * The ability identifier is a navigation argument, so it arrives through the
 * saved-state handle and survives process death without the screen having to
 * remember it.
 */
class SkillDetailViewModel @Inject constructor(
    private val skills: SkillsRepository,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    private val skillId = savedState.get<String>(SKILL_ID_KEY).orEmpty()
    private val removeResult = MutableStateFlow<SkillsRepository.RemoveResult?>(null)

    private val acceptResult = MutableStateFlow<Acceptance?>(null)
    private val reviewLoad = MutableStateFlow<ReviewLoad?>(null)

    /** What screen SCR-602 renders. */
    val state: StateFlow<SkillDetailUiState> =
        combine(skills.snapshot, removeResult, acceptResult, reviewLoad) { snapshot, result, acceptance, load ->
            val projected = projectSkillDetail(snapshot, skillId, result)
            projected.copy(acceptResult = acceptance?.takeIf {
                projected.skill?.review == it.review
            }?.result,
                reviewLoading = load?.version == projected.skill?.version && load?.loading == true,
                reviewFailed = load?.version == projected.skill?.version && load?.failed == true,
            )
        }.stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectSkillDetail(skills.snapshot.value, skillId),
        )

    /** Act on something the user did. */
    fun onIntent(intent: SkillDetailIntent) {
        reduceSkillDetail(state.value, intent)
        when (intent) {
            SkillDetailIntent.LoadRecordedReview -> viewModelScope.launch {
                val current = skills.snapshot.value.skills.singleOrNull { it.id == skillId } ?: return@launch
                if (current.review != null || reviewLoad.value?.loading == true) return@launch
                val version = current.version
                reviewLoad.value = ReviewLoad(version, loading = true)
                val loaded = skills.loadRecordedReview(skillId, version)
                reviewLoad.value = ReviewLoad(version, failed = !loaded)
            }
            is SkillDetailIntent.AcceptRecorded -> viewModelScope.launch {
                if (intent.review.id != skillId ||
                    (acceptResult.value?.review == intent.review &&
                        acceptResult.value?.result == SkillsRepository.MutationResult.SUBMITTED)
                ) return@launch
                acceptResult.value = Acceptance(intent.review, SkillsRepository.MutationResult.SUBMITTED)
                acceptResult.value = Acceptance(intent.review, skills.acceptRecorded(intent.review))
            }
            SkillDetailIntent.Toggle -> viewModelScope.launch {
                val current = skills.snapshot.value.skills.firstOrNull { it.id == skillId }
                    ?: return@launch
                if (!current.needsRecordedReview) skills.setEnabled(skillId, !current.enabled)
            }
            SkillDetailIntent.Remove -> viewModelScope.launch {
                removeResult.value = skills.remove(skillId)
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.SkillDetail(skillId).screenId),
        )
    }

    private data class ReviewLoad(val version: UInt, val loading: Boolean = false, val failed: Boolean = false)

    private data class Acceptance(
        val review: SavedFlowReview,
        val result: SkillsRepository.MutationResult,
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        val SKILL_ID_KEY = TaffyDestination.SKILL_ID
    }
}
