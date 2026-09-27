// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.api.acceptanceMutation
import com.taffygo.browser.ui.core.api.needsRecordedReview
import com.taffygo.browser.ui.core.api.toSavedFlowReview
import com.taffygo.browser.ui.core.model.SavedFlowReview
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.BuiltinSkillAvailabilityView
import taffy.core_api.BuiltinSkillView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.MAX_SKILL_ARGUMENTS_PER_STEP
import taffy.core_api.MAX_SKILL_ID_BYTES
import taffy.core_api.MAX_SKILL_MATCH_CLAUSES
import taffy.core_api.MAX_SKILL_ORIGIN_BYTES
import taffy.core_api.MAX_SKILL_STEPS
import taffy.core_api.MAX_SKILL_TOOL_NAME_BYTES
import taffy.core_api.SiteSkillMutationBody
import taffy.core_api.SiteSkillMutationKind
import taffy.core_api.SiteSkillStatusView

/** Window-facing projection of the profile's durable assistant abilities. */
internal class CoreSkillsRepository(
    private val core: CoreApiClient,
    lifetime: TaffyProfileLifetime,
    private val reviews: SavedFlowReviewRepository? = null,
) : SkillsRepository {

    override val snapshot: StateFlow<SkillsRepository.Snapshot> = (reviews?.status ?: core.status)
        .distinctUntilChanged(::sameSkillsProjectionVersion)
        .map(CoreStatus::toSkillsSnapshot)
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.toSkillsSnapshot(),
        )

    override suspend fun setEnabled(id: String, enabled: Boolean) {
        val builtin = core.status.value.readyBuiltinSkill(id)
        if (builtin != null) {
            if (builtin.availability != BuiltinSkillAvailabilityView.AVAILABLE) return
            val current = core.status.value.readyAssistantConfiguration() ?: return
            val disabled = current.disabled_abilities.toMutableSet()
            if (enabled) {
                disabled.remove(builtin.required_ability)
            } else {
                disabled.add(builtin.required_ability)
            }
            core.replaceAssistantConfiguration(current, disabledAbilities = disabled.toList())
            return
        }
        val current = core.status.value.readySiteSkill(id) ?: return
        if (enabled && current.needsRecordedReview()) return
        core.mutateSiteSkill(
            emptyMutation(
                kind = SiteSkillMutationKind.SET_ENABLED,
                id = id,
                version = current.active_version,
                enabled = enabled,
            ),
        )
    }

    override suspend fun loadRecordedReview(id: String, version: UInt): Boolean = reviews?.load(id, version) == true

    override suspend fun acceptRecorded(review: SavedFlowReview): SkillsRepository.MutationResult {
        val current = (reviews?.current() ?: core.status.value).readySiteSkill(review.id)
            ?: return SkillsRepository.MutationResult.UNAVAILABLE
        if (!current.needsRecordedReview() || current.toSavedFlowReview() != review) {
            return SkillsRepository.MutationResult.STALE
        }
        return try {
            core.mutateSiteSkill(review.acceptanceMutation())
            SkillsRepository.MutationResult.SUBMITTED
        } catch (failure: CoreApiSubmissionException) {
            failure.toMutationResult()
        }
    }

    override suspend fun remove(id: String): SkillsRepository.RemoveResult {
        if (!core.status.value.hasCompleteProjection()) {
            return SkillsRepository.RemoveResult.UNAVAILABLE
        }
        if (core.status.value.readyBuiltinSkill(id) != null) {
            return SkillsRepository.RemoveResult.CANNOT_REMOVE
        } else {
            val skill = core.status.value.readySiteSkill(id)
                ?: return SkillsRepository.RemoveResult.NOT_FOUND
            return try {
                core.mutateSiteSkill(
                    emptyMutation(
                        kind = SiteSkillMutationKind.REMOVE,
                        id = id,
                        version = skill.active_version,
                    ),
                )
                SkillsRepository.RemoveResult.REMOVED
            } catch (_: CoreApiSubmissionException) {
                SkillsRepository.RemoveResult.UNAVAILABLE
            }
        }
    }

    override suspend fun teachSite(
        name: String,
        recording: SkillsRepository.ObservedSiteRecording,
    ): SkillsRepository.MutationResult {
        if (!core.status.value.hasCompleteProjection()) {
            return SkillsRepository.MutationResult.UNAVAILABLE
        }
        val id = plainSkillId(name) ?: return SkillsRepository.MutationResult.INVALID
        if (core.status.value.site_skills.any { it.skill_id == id }) {
            return SkillsRepository.MutationResult.ALREADY_EXISTS
        }
        return submitRecording(SiteSkillMutationKind.TEACH, id, 0u, recording)
    }

    override suspend fun updateSite(
        id: String,
        expectedVersion: UInt,
        recording: SkillsRepository.ObservedSiteRecording,
    ): SkillsRepository.MutationResult {
        val current = core.status.value.readySiteSkill(id)
            ?: return if (core.status.value.hasCompleteProjection()) {
                SkillsRepository.MutationResult.NOT_FOUND
            } else {
                SkillsRepository.MutationResult.UNAVAILABLE
            }
        if (current.active_version != expectedVersion) {
            return SkillsRepository.MutationResult.STALE
        }
        return submitRecording(SiteSkillMutationKind.UPDATE, id, expectedVersion, recording)
    }

    private suspend fun submitRecording(
        kind: SiteSkillMutationKind,
        id: String,
        version: UInt,
        recording: SkillsRepository.ObservedSiteRecording,
    ): SkillsRepository.MutationResult {
        if (!recording.isBounded()) return SkillsRepository.MutationResult.INVALID
        return try {
            core.mutateSiteSkill(
                SiteSkillMutationBody(
                    kind = kind,
                    skill_id = id,
                    expected_version = version,
                    origin = recording.origin,
                    clauses = recording.clauses,
                    steps = recording.steps,
                    admitted = recording.steps.size.toUInt(),
                    enabled = false,
                    recorded_at_epoch_ms = 0uL,
                ),
            )
            SkillsRepository.MutationResult.SUBMITTED
        } catch (failure: CoreApiSubmissionException) {
            failure.toMutationResult()
        }
    }
}

/** Allocation-free comparison of exactly the generated fields the Skills surface renders. */
internal fun sameSkillsProjectionVersion(previous: CoreStatus, current: CoreStatus): Boolean {
    if (previous.generation != current.generation || previous.availability != current.availability ||
        previous.projection_mode != current.projection_mode ||
        previous.builtin_skills != current.builtin_skills ||
        previous.site_skills.size != current.site_skills.size
    ) {
        return false
    }
    return previous.site_skills.indices.all { index ->
        val before = previous.site_skills[index]
        val after = current.site_skills[index]
        before.skill_id == after.skill_id &&
            before.origin == after.origin &&
            before.status == after.status &&
            before.active_version == after.active_version &&
            before.step_count == after.step_count &&
            before.provenance == after.provenance &&
            before.recorded_from_task_id == after.recorded_from_task_id &&
            before.reviewed_steps == after.reviewed_steps
    }
}

private fun CoreStatus.toSkillsSnapshot(): SkillsRepository.Snapshot = when (availability) {
    CoreAvailability.STARTING ->
        SkillsRepository.Snapshot(SkillsRepository.Availability.LOADING)
    CoreAvailability.READY -> {
        val builtins = builtin_skills.toSkillsOrNull()
            ?: return SkillsRepository.Snapshot(SkillsRepository.Availability.UNAVAILABLE)
        val siteSkillsAvailable = projection_mode == CoreStatusProjectionMode.COMPLETE
        SkillsRepository.Snapshot(
            availability = SkillsRepository.Availability.READY,
            skills = builtins + site_skills.takeIf { siteSkillsAvailable }.orEmpty()
                .sortedBy { it.skill_id }.map { skill ->
                SkillsRepository.Skill(
                    id = skill.skill_id,
                    group = SkillsRepository.Group.PAGES,
                    mayUse = listOf(SkillsRepository.MayUse.PAGES),
                    enabled = skill.status == SiteSkillStatusView.ACTIVE,
                    builtIn = false,
                    readiness = SkillsRepository.Readiness.READY,
                    origin = skill.origin,
                    version = skill.active_version,
                    stepCount = skill.step_count,
                    lifecycle = skill.status.toLifecycle(),
                    needsRecordedReview = skill.needsRecordedReview(),
                    recorded = skill.recorded_from_task_id != null,
                    review = skill.toSavedFlowReview(),
                )
            },
            siteSkillsAvailable = siteSkillsAvailable,
        )
    }
    CoreAvailability.UNAVAILABLE,
    CoreAvailability.CIRCUIT_OPEN,
    -> SkillsRepository.Snapshot(SkillsRepository.Availability.UNAVAILABLE)
}

private fun CoreStatus.readySiteSkill(id: String) =
    takeIf { hasCompleteProjection() }
        ?.site_skills
        ?.firstOrNull { it.skill_id == id }

private fun CoreStatus.readyBuiltinSkill(id: String): BuiltinSkillView? {
    if (availability != CoreAvailability.READY || builtin_skills.toSkillsOrNull() == null) {
        return null
    }
    val expected = id.toBuiltinSkillId() ?: return null
    return builtin_skills.firstOrNull { it.reference.skill_id == expected }
}

private fun emptyMutation(
    kind: SiteSkillMutationKind,
    id: String,
    version: UInt,
    enabled: Boolean = false,
) = SiteSkillMutationBody(
    kind = kind,
    skill_id = id,
    expected_version = version,
    origin = "",
    clauses = emptyList(),
    steps = emptyList(),
    admitted = 0u,
    enabled = enabled,
    recorded_at_epoch_ms = 0uL,
)

private fun SkillsRepository.ObservedSiteRecording.isBounded(): Boolean =
    origin.isNotBlank() && origin.encodeToByteArray().size <= MAX_SKILL_ORIGIN_BYTES &&
        clauses.isNotEmpty() && clauses.size <= MAX_SKILL_MATCH_CLAUSES &&
        steps.isNotEmpty() && steps.size <= MAX_SKILL_STEPS &&
        steps.all { step ->
            step.verb.isNotEmpty() &&
                step.verb.encodeToByteArray().size <= MAX_SKILL_TOOL_NAME_BYTES &&
                step.arguments.size <= MAX_SKILL_ARGUMENTS_PER_STEP
        }

internal fun plainSkillId(name: String): String? {
    val id = name.trim().lowercase()
        .replace(Regex("[^a-z0-9.]+"), "-")
        .trim('-')
    return id.takeIf { it.isNotEmpty() && it.encodeToByteArray().size <= MAX_SKILL_ID_BYTES }
}

internal fun plainSkillName(id: String): String = id
    .split('-', '.')
    .filter(String::isNotEmpty)
    .joinToString(" ") { word -> word.replaceFirstChar(Char::uppercase) }

private fun SiteSkillStatusView.toLifecycle(): SkillsRepository.Lifecycle = when (this) {
    SiteSkillStatusView.DRAFT -> SkillsRepository.Lifecycle.DRAFT
    SiteSkillStatusView.ACTIVE -> SkillsRepository.Lifecycle.ACTIVE
    SiteSkillStatusView.SUPERSEDED -> SkillsRepository.Lifecycle.SUPERSEDED
    SiteSkillStatusView.RETIRED -> SkillsRepository.Lifecycle.RETIRED
    SiteSkillStatusView.DISABLED -> SkillsRepository.Lifecycle.DISABLED
}

private fun CoreApiSubmissionException.toMutationResult(): SkillsRepository.MutationResult =
    when (reason) {
        CoreApiSubmissionException.Reason.STALE_REVISION -> SkillsRepository.MutationResult.STALE
        CoreApiSubmissionException.Reason.CORE_UNAVAILABLE,
        CoreApiSubmissionException.Reason.STALE_GENERATION,
        CoreApiSubmissionException.Reason.DEADLINE_EXCEEDED,
        CoreApiSubmissionException.Reason.BACKPRESSURE,
        -> SkillsRepository.MutationResult.UNAVAILABLE
        CoreApiSubmissionException.Reason.INVALID_REQUEST,
        CoreApiSubmissionException.Reason.DUPLICATE,
        CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION,
        // Start refusals only; a skill mutation that met one met a browser
        // outside its own contract, and that fails closed.
        CoreApiSubmissionException.Reason.SOURCE_NOT_OPEN,
        CoreApiSubmissionException.Reason.SOURCE_AMBIGUOUS,
        CoreApiSubmissionException.Reason.WINDOW_UNAVAILABLE,
        -> SkillsRepository.MutationResult.INVALID
    }
