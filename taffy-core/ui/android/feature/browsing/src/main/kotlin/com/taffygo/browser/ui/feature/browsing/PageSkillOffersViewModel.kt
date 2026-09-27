// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.api.toSavedFlowReview
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.page.PageIntelligenceRepository
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartFailure
import com.taffygo.browser.ui.core.task.savedFlowStartRequest
import com.taffygo.browser.ui.core.task.taskStartRequest
import com.taffygo.browser.ui.core.task.toTaskStartFailure
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import taffy.core_api.CoreStatus
import taffy.core_api.PageInspectorAvailability
import taffy.core_api.PageInspectorDocumentState
import taffy.core_api.SiteSkillOfferAvailability
import taffy.core_api.SiteSkillOfferView
import taffy.core_api.SiteSkillStatusView

/** Explicit semantic inspection and exact-offer submission. The browser owns document admission. */
class PageSkillOffersViewModel(
    private val browser: BrowserRepository,
    private val pages: PageIntelligenceRepository,
    private val core: StateFlow<CoreStatus>,
    private val tasks: TaskRepository,
    private val readiness: TaffyReadinessRepository,
    private val reviews: SavedFlowReviewRepository? = null,
    flowPages: SavedFlowPageRequests? = null,
) : ViewModel() {
    private val loaded = MutableStateFlow(Loaded())
    private var loading: Job? = null

    val state: StateFlow<PageSkillOffersUiState> = combine(
        loaded, core, tasks.state, readiness.readiness,
    ) { page, status, taskState, ready -> project(page, status, taskState, ready) }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000L), PageSkillOffersUiState())

    init {
        if (flowPages != null) viewModelScope.launch {
            flowPages.state.filterNotNull().collectLatest { request ->
                val settled = withTimeoutOrNull(10_000L) {
                    combine(browser.tabs, browser.navigation) { tabs, navigation ->
                        tabs.singleOrNull { it.isSelected } to navigation
                    }.first { (tab, navigation) ->
                        tab?.id != request.tabId || tab.isPrivate || tab.isTaffyTab ||
                            (!navigation.isLoading && navigation.canonicalUrl == request.review.startingAddress)
                    }
                }
                flowPages.consumed(request)
                if (settled?.first?.id == request.tabId && currentPage()?.address == request.review.startingAddress) {
                    open(request.goal)
                } else if (settled == null && browser.tabs.value.singleOrNull { it.isSelected }?.id == request.tabId) {
                    loaded.value = Loaded(open = true, availability = PageSkillOffersUiState.Availability.UNAVAILABLE)
                }
            }
        }
        viewModelScope.launch {
            combine(browser.tabs, browser.navigation) { _, _ -> currentPage() }
                .distinctUntilChanged().collect { current ->
                    val shown = loaded.value
                    if (shown.open && !shown.starting && shown.page != current) {
                        loading?.cancel()
                        loaded.value = shown.copy(offers = emptyList(), availability = PageSkillOffersUiState.Availability.STALE)
                    }
                }
        }
    }

    fun open(goal: String) {
        loading?.cancel()
        val page = currentPage()
        if (page == null || !pages.isAvailable) {
            loaded.value = Loaded(open = true, availability = PageSkillOffersUiState.Availability.UNAVAILABLE)
            return
        }
        loaded.value = Loaded(open = true, page = page, goal = goal)
        loading = viewModelScope.launch {
            val result = withTimeoutOrNull(10_000L) {
                val documents = pages.documents()
                if (documents.availability != PageInspectorAvailability.AVAILABLE) return@withTimeoutOrNull null
                val document = documents.documents.singleOrNull() ?: return@withTimeoutOrNull null
                pages.snapshot(document.document_id).takeIf {
                    it.availability == PageInspectorAvailability.AVAILABLE &&
                        it.snapshot?.document_id == document.document_id
                }?.snapshot
            }
            if (!loaded.value.open || currentPage() != page) return@launch
            loaded.value = if (result == null || result.private_profile ||
                result.document_state != PageInspectorDocumentState.ACTIVE ||
                result.site_skill_offer_availability != SiteSkillOfferAvailability.AVAILABLE
            ) {
                loaded.value.copy(availability = PageSkillOffersUiState.Availability.UNAVAILABLE)
            } else {
                loaded.value.copy(
                    host = result.host,
                    offers = result.site_skill_offers,
                    availability = if (result.site_skill_offers.isEmpty()) {
                        PageSkillOffersUiState.Availability.EMPTY
                    } else PageSkillOffersUiState.Availability.READY,
                )
            }
        }
    }

    fun loadReviews() {
        val shown = loaded.value
        if (!shown.open || shown.starting || shown.reviewLoading || shown.page != currentPage()) return
        loaded.value = shown.copy(reviewLoading = true)
        loading = viewModelScope.launch {
            shown.offers.take(4).forEach { offer ->
                val skill = core.value.site_skills.singleOrNull {
                    it.skill_id == offer.skill_id && it.active_version == offer.active_version
                }
                if (skill?.recorded_from_task_id != null && skill.toSavedFlowReview() == null) {
                    reviews?.load(offer.skill_id, offer.active_version)
                }
            }
            if (loaded.value.page == shown.page && currentPage() == shown.page) {
                loaded.value = loaded.value.copy(reviewLoading = false)
            }
        }
    }

    fun close() {
        if (loaded.value.starting) return
        loading?.cancel()
        loaded.value = Loaded()
    }

    /** Uses the displayed opaque identity; a fresh inspection never substitutes another offer. */
    fun start(offer: PageSkillOffersUiState.Offer, navigator: TaffyNavigator) {
        val shown = loaded.value
        if (!shown.open || shown.starting) return
        if (shown.page != currentPage() ||
            project(shown, core.value, tasks.state.value, readiness.readiness.value).offers.none { it == offer }
        ) {
            loaded.value = shown.copy(offers = emptyList(), availability = PageSkillOffersUiState.Availability.STALE)
            return
        }
        val request = (offer.start as? TaskStartDecision.Start)?.request ?: return
        val previous = tasks.state.value.tasks.map { it.id }.toSet()
        loaded.value = shown.copy(starting = true, failure = null)
        viewModelScope.launch {
            val result = tasks.startTask(request.goal, request.template, request.consent, null, offer.id)
            if (result is TaffyResult.Failure) {
                loaded.value = loaded.value.copy(starting = false, failure = result.reason.toTaskStartFailure())
                return@launch
            }
            val started = withTimeoutOrNull(5_000L) {
                tasks.state.map { it.tasks.firstOrNull { task -> task.id !in previous } }
                    .filterNotNull().first()
            }
            if (started == null) {
                loaded.value = loaded.value.copy(starting = false, failure = TaskStartFailure.DEADLINE_EXCEEDED)
            } else {
                tasks.follow(started.id)
                loaded.value = Loaded()
                navigator.goTo(TaffyDestination.TaskView)
            }
        }
    }

    private fun currentPage(): Page? {
        val tab = browser.tabs.value.singleOrNull { it.isSelected } ?: return null
        val navigation = browser.navigation.value
        if (tab.isPrivate || tab.hasBeenNowhere || navigation.isLoading ||
            navigation.canonicalUrl.isBlank()
        ) return null
        return Page(tab.id, navigation.canonicalUrl)
    }

    private fun project(
        page: Loaded,
        status: CoreStatus,
        tasks: TaskRepositoryState,
        ready: TaffyReadiness,
    ): PageSkillOffersUiState {
        if (!page.open) return PageSkillOffersUiState()
        val base = PageSkillOffersUiState(
            open = true, host = page.host, availability = page.availability,
            starting = page.starting, failure = page.failure, reviewLoading = page.reviewLoading,
        )
        if (page.availability != PageSkillOffersUiState.Availability.READY) return base
        if (!status.hasCompleteProjection()) return base.copy(availability = PageSkillOffersUiState.Availability.UNAVAILABLE)
        var stale = false
        var unavailableReview = false
        val offers = page.offers.mapNotNull { offer ->
            val skill = status.site_skills.singleOrNull {
                it.skill_id == offer.skill_id && it.active_version == offer.active_version &&
                    it.status == SiteSkillStatusView.ACTIVE && it.step_count == offer.step_count
            } ?: run { stale = true; return@mapNotNull null }
            val learned = skill.recorded_from_task_id != null
            val review = skill.toSavedFlowReview()
            if (learned && review == null) {
                unavailableReview = true
                return@mapNotNull null
            }
            PageSkillOffersUiState.Offer(
                id = offer.offer_id, skillId = offer.skill_id, version = offer.active_version,
                stepCount = offer.step_count, review = review,
                start = if (learned) savedFlowStartRequest(
                    goal = page.goal, sourceHost = page.host, availability = tasks.availability,
                    taskAlreadyRunning = tasks.tasks.any { it.isUnderWay },
                ) else taskStartRequest(
                    goal = page.goal,
                    template = TaskTemplate.BUILD_A_SOURCE_TABLE,
                    sourceHosts = listOf(page.host), readiness = ready, availability = tasks.availability,
                    taskAlreadyRunning = tasks.tasks.any { it.isUnderWay },
                ),
            )
        }
        return if (stale) {
            base.copy(availability = PageSkillOffersUiState.Availability.STALE)
        } else base.copy(
            offers = offers,
            availability = if (unavailableReview) PageSkillOffersUiState.Availability.REVIEW_UNAVAILABLE
                else PageSkillOffersUiState.Availability.READY,
        )
    }

    private data class Page(val tabId: TabId, val address: String)
    private data class Loaded(
        val open: Boolean = false,
        val page: Page? = null,
        val host: String = "",
        val goal: String = "",
        val offers: List<SiteSkillOfferView> = emptyList(),
        val availability: PageSkillOffersUiState.Availability = PageSkillOffersUiState.Availability.LOADING,
        val starting: Boolean = false,
        val reviewLoading: Boolean = false,
        val failure: TaskStartFailure? = null,
    )
}
