// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import com.taffygo.browser.ui.core.model.SavedFlowReview
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import taffy.core_api.CoreStatus
import taffy.core_api.SavedFlowQueryAvailability
import taffy.core_api.SavedFlowQueryResult
import taffy.core_api.SiteSkillStatusView
import taffy.core_api.SiteSkillView

/** Transient complete reviews. Matching and navigation authority remain in the browser/core. */
class SavedFlowReviewRepository(private val core: CoreApiClient, scope: CoroutineScope) {
    private val cache = MutableStateFlow<Cache?>(null)
    private var serial = 0L
    private val requests = Mutex()
    private val session = UUID.randomUUID().toString()

    val status: StateFlow<CoreStatus> = combine(core.status, cache, ::project)
        .stateIn(scope, SharingStarted.Eagerly, core.status.value)

    /** Null is an unavailable query; an empty list is an authoritative no-match reply. */
    suspend fun find(goal: String): List<SavedFlowReview>? = query(
        expected = { flows -> flows.all { it.status == SiteSkillStatusView.ACTIVE } },
    ) { request ->
        core.findSavedFlows(request, goal)
    }?.mapNotNull(SiteSkillView::toSavedFlowReview)

    suspend fun load(id: String, version: UInt): Boolean = query(
        expected = { flows -> flows.singleOrNull()?.let { it.skill_id == id && it.active_version == version } == true },
    ) { request ->
        core.getSavedFlowReview(request, id, version)
    } != null

    /** Recheck the exact displayed review before asking the browser to revalidate and navigate. */
    suspend fun open(review: SavedFlowReview): Boolean = requests.withLock { openReviewed(review) }

    private suspend fun openReviewed(review: SavedFlowReview): Boolean {
        val before = current()
        val skill = before.site_skills.singleOrNull { it.toSavedFlowReview() == review }
        if (!before.hasCompleteProjection() || skill?.status != SiteSkillStatusView.ACTIVE) return false
        val ticket = ++serial
        return try {
            withTimeoutOrNull(10_000L) {
                core.openSavedFlowStart("$session-$ticket", review.id, review.version)
                ticket == serial && core.status.value.generation == before.generation &&
                    current().site_skills.any { it.status == SiteSkillStatusView.ACTIVE && it.toSavedFlowReview() == review }
            } == true
        } catch (_: CoreApiSubmissionException) {
            false
        }
    }

    /** Synchronous validation reads the live core, never the asynchronously combined projection. */
    fun current(): CoreStatus = project(core.status.value, cache.value)

    private suspend fun query(
        expected: (List<SiteSkillView>) -> Boolean,
        call: suspend (String) -> SavedFlowQueryResult,
    ): List<SiteSkillView>? = requests.withLock { queryCurrent(expected, call) }

    private suspend fun queryCurrent(
        expected: (List<SiteSkillView>) -> Boolean,
        call: suspend (String) -> SavedFlowQueryResult,
    ): List<SiteSkillView>? {
        val before = core.status.value
        if (!before.hasCompleteProjection()) return null
        val ticket = ++serial
        val request = "$session-$ticket"
        val reply = try {
            withTimeoutOrNull(10_000L) { call(request) }
        } catch (_: CoreApiSubmissionException) {
            null
        } ?: return null
        val now = core.status.value
        if (ticket != serial || reply.request_id != request ||
            reply.service_generation != before.generation || now.generation != before.generation ||
            !now.hasCompleteProjection() || reply.availability != SavedFlowQueryAvailability.AVAILABLE ||
            !expected(reply.flows) || reply.flows.size > 4 || reply.flows.distinctBy { it.skill_id }.size != reply.flows.size ||
            reply.flows.any { flow -> flow.toSavedFlowReview() == null || now.site_skills.none { sameMetadata(it, flow) } }
        ) return null
        val retained = cache.value?.takeIf { it.generation == now.generation }?.flows.orEmpty()
        cache.value = Cache(now.generation, (reply.flows + retained).distinctBy { it.skill_id }.take(4))
        return reply.flows
    }

    private fun project(status: CoreStatus, stored: Cache?): CoreStatus {
        if (!status.hasCompleteProjection() || stored?.generation != status.generation) return status
        return status.copy(site_skills = status.site_skills.map { skill ->
            if (skill.reviewed_steps.isNotEmpty()) skill
            else stored.flows.firstOrNull { sameMetadata(it, skill) } ?: skill
        })
    }

    private fun sameMetadata(left: SiteSkillView, right: SiteSkillView): Boolean =
        left.copy(reviewed_steps = emptyList()) == right.copy(reviewed_steps = emptyList())

    private data class Cache(val generation: ULong, val flows: List<SiteSkillView>)
}
