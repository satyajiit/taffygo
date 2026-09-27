// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.api.submitCoreApiCommand
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.TaskArtifactPayload
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskControlRefusal
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.TaskControl
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.buffer
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.produceIn
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import taffy.core_api.TaskTemplateId
import taffy.core_api.TaskAttachedStore as CoreTaskAttachedStore
import taffy.core_api.TaskConsentPreview
import taffy.core_api.TaskProviderRoute
import taffy.core_api.TaskArtifactKind

/** The only Android task repository: a projection over the browser Core API. */
internal class CoreTaskRepository(
    private val core: CoreApiClient,
    private val analytics: AnalyticsClient,
    lifetime: TaffyProfileLifetime,
) : TaskRepository {
    private val liveAnswer = MutableStateFlow<LiveTaskAnswer?>(null)

    /**
     * The follow-ups sent from this process, oldest first (decision 0137).
     * Residency only: a restart forgets them with the answers they pair to.
     */
    private val questions = MutableStateFlow<List<AskedQuestion>>(emptyList())

    override val artifactExports = core.taskArtifactExport.map { report ->
        TaskArtifactPayload(
            requestId = report.requestId,
            taskId = report.taskId,
            artifactId = report.artifactId,
            kind = report.kind,
            content = report.content,
        )
    }

    /** The task id a surface asked to follow; null follows whichever is under way. */
    private val followedTaskId = MutableStateFlow<String?>(null)

    /**
     * The ended tasks a person has put away, newest last (decision 0149).
     *
     * Residency only, like [questions]: a restart forgets them, which is
     * exactly right, because the core already leaves out anything that ended
     * before this browser run. Bounded because it is a set a surface writes
     * to and nothing ever prunes — the oldest id falls out at the cap, and the
     * task it named is one the core has almost certainly stopped publishing.
     */
    private val putAwayTaskIds = MutableStateFlow<Set<String>>(emptySet())

    private val coreState: StateFlow<TaskRepositoryState> = combine(
        core.status,
        followedTaskId,
        putAwayTaskIds,
    ) { status, followed, putAway -> Triple(status, followed, putAway) }
        // Answer deltas arrive on their own push channel. Projecting the
        // active workspace for every unrelated status publication used to
        // rebuild as many as 256 facts and 64 sources on the UI path.
        .distinctUntilChangedBy { (status, followed, putAway) ->
            status.taskProjectionVersion(followed, putAway)
        }
        .map { (status, followed, putAway) -> status.toRepositoryState(followed, putAway) }
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.toRepositoryState(
                followedTaskId.value,
                putAwayTaskIds.value,
            ),
        )

    override fun follow(taskId: String?) {
        followedTaskId.value = taskId
    }

    override fun putAway(taskId: String) {
        // Only a task that has ended right now. Recording an id for a task
        // that is still running would be a dismissal that fires whenever it
        // finishes — a surface hiding work the person never saw end — and the
        // filter alone cannot tell the two apart, because by then the phase
        // says the same thing either way.
        val ended = core.status.value.active_tasks
            .firstOrNull { it.task_id == taskId }
            ?.phase
            ?.hasEnded() == true
        if (!ended) return
        putAwayTaskIds.update { ids ->
            if (taskId in ids) {
                ids
            } else {
                (ids + taskId).let { grown ->
                    if (grown.size <= MAX_PUT_AWAY_TASKS) grown else grown.drop(1).toSet()
                }
            }
        }
        // The pin has to go too. `followedTask` prefers the followed id, and
        // when that id names nothing it falls through to the first task in the
        // list — which, for a filtered-out task, would be the same task again.
        if (followedTaskId.value == taskId) followedTaskId.value = null
    }

    /** The last control the browser would not take, for every surface at once. */
    private val controlRefusal = MutableStateFlow<TaskControlRefusal?>(null)

    override val state: StateFlow<TaskRepositoryState> = combine(
        coreState,
        liveAnswer,
        questions,
        controlRefusal,
    ) { stable, answer, asked, refused ->
        // Kept only while it still names the task revision the core is
        // publishing. A refusal about a revision that has moved on is about a
        // screen nobody is looking at any more.
        val followed = stable.task
        stable.withConversation(answer, asked).copy(
            controlRefusal = refused?.takeIf { refusal ->
                followed != null &&
                    followed.id == refusal.taskId &&
                    followed.revision == refusal.taskRevision
            },
        )
    }
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = coreState.value,
        )

    init {
        val events = core.taskAnswer.buffer(TASK_ANSWER_EVENT_BUFFER).produceIn(lifetime.scope)
        lifetime.scope.launch {
            val accumulator = TaskAnswerAccumulator()
            while (isActive) {
                val first = events.receiveCatching().getOrNull() ?: break
                accumulator.accept(first)
                // Coalesce token-sized deltas to one immutable UI projection
                // per frame. The accumulator itself still sequence-checks
                // every event and terminal events are included in the same
                // batch; no semantic event is sampled away.
                if (!first.terminal) delay(TASK_ANSWER_UI_FRAME_MS)
                while (true) {
                    val next = events.tryReceive().getOrNull() ?: break
                    accumulator.accept(next)
                }
                accumulator.snapshot()?.let { answer ->
                    // Stamped with the generation that is speaking. A call the
                    // core gives up when its utility process goes emits no
                    // terminal, so without this the last streaming projection
                    // is the one this process keeps for ever.
                    liveAnswer.value = LiveTaskAnswer(
                        taskId = answer.taskId,
                        generation = core.status.value.generation,
                        calls = answer.calls,
                    )
                }
            }
        }
    }

    override suspend fun startTask(
        goal: String,
        template: TaskTemplate,
        consent: TaskConsentIntent,
        workspaceId: String?,
    ): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        return submitCoreApiCommand {
            core.startTask(goal, template.toCoreTemplate(), consent.toCorePreview(), workspaceId)
        }
    }

    override suspend fun startTask(
        goal: String,
        template: TaskTemplate,
        consent: TaskConsentIntent,
        workspaceId: String?,
        skillOfferId: String?,
    ): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        return submitCoreApiCommand {
            core.startTask(
                goal,
                template.toCoreTemplate(),
                consent.toCorePreview(),
                workspaceId,
                skillOfferId,
            )
        }
    }

    override suspend fun cancelTask(taskId: String): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        if (!state.value.admits(taskId, TaskControl.STOP)) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        return submitCoreApiCommand { core.cancelTask(taskId) }
    }

    override suspend fun useControl(
        taskId: String,
        taskRevision: ULong,
        control: TaskControl,
    ): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        if (!state.value.admits(taskId, taskRevision, control)) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        // The answer is read rather than returned and forgotten. These
        // commands are answered on admission, so a refused control changes
        // nothing a person can see, which is indistinguishable from a control
        // that worked. Resume on a task paused before a browser restart is
        // refused at `authority/resume` every time (decision 0221).
        val result = submitCoreApiCommand {
            analytics.record(AnalyticsEvent.TaskControlUsed(control.label))
            when (control) {
                TaskControl.PAUSE -> core.pauseTask(taskId)
                TaskControl.RESUME -> core.resumeTask(taskId)
                TaskControl.TAKE_OVER -> core.takeOver(taskId)
                TaskControl.STOP -> core.cancelTask(taskId)
            }
        }
        controlRefusal.value = when (result) {
            is TaffyResult.Success -> null
            is TaffyResult.Failure ->
                TaskControlRefusal(taskId, taskRevision, control, result.reason)
        }
        return result
    }

    override suspend fun completeHandover(taskId: String): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        return submitCoreApiCommand { core.completeHandover(taskId) }
    }

    override suspend fun supplyUserInput(taskId: String, answer: String): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        return submitCoreApiCommand { core.supplyUserInput(taskId, answer) }
    }

    override suspend fun followUp(taskId: String, question: String): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        // The mark is read before the send, so a reply that starts streaming
        // between admission and the update still lands after it.
        val afterCalls = liveAnswer.value?.takeIf { it.taskId == taskId }?.calls?.size ?: 0
        val result = submitCoreApiCommand { core.followUp(taskId, question) }
        if (result is TaffyResult.Success) {
            questions.update { asked ->
                (asked + AskedQuestion(taskId, question, afterCalls)).takeLast(MAX_REMEMBERED_QUESTIONS)
            }
        }
        return result
    }

    override suspend fun approveAction(taskId: String, actionId: String): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        return submitCoreApiCommand { core.approveAction(taskId, actionId) }
    }

    override suspend fun acceptTaskArtifact(
        taskId: String,
        artifactId: String,
    ): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        val artifact = state.value.task
            ?.takeIf { it.id == taskId }
            ?.artifacts
            ?.firstOrNull { it.id == artifactId && !it.accepted }
            ?: return TaffyResult.Failure(FailureReason.STALE_REVISION)
        return submitCoreApiCommand { core.acceptTaskArtifact(taskId, artifact.id) }
    }

    override suspend fun requestTaskArtifactExport(
        requestId: String,
        taskId: String,
        artifactId: String,
        kind: TaskArtifactKind,
    ): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        val artifact = state.value.task
            ?.takeIf { it.id == taskId }
            ?.artifacts
            ?.firstOrNull { it.id == artifactId && it.kind == kind && it.accepted }
            ?: return TaffyResult.Failure(FailureReason.STALE_REVISION)
        return submitCoreApiCommand {
            core.requestTaskArtifactExport(requestId, taskId, artifact.id, artifact.kind)
        }
    }

    override suspend fun saveWorkspace(
        workspaceId: String,
        workspaceRevision: ULong,
    ): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        val task = state.value.task
        if (task?.workspaceId != workspaceId ||
            task.workspaceRevision != workspaceRevision ||
            !task.canSaveWorkspace
        ) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        return submitCoreApiCommand { core.saveWorkspace(workspaceId, workspaceRevision) }
    }

    override suspend fun discardWorkspace(
        workspaceId: String,
        workspaceRevision: ULong,
    ): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        val task = state.value.task
        if (task?.workspaceId != workspaceId ||
            task.workspaceRevision != workspaceRevision ||
            !task.canDiscardWorkspace
        ) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        return submitCoreApiCommand { core.discardWorkspace(workspaceId, workspaceRevision) }
    }

    override suspend fun retryCore() = submitCoreApiCommand {
        core.retryCore()
    }
}

/** Exact local stale-tap check over one immutable browser publication. */
internal fun TaskRepositoryState.admits(
    taskId: String,
    taskRevision: ULong,
    control: TaskControl,
): Boolean = task?.let {
    it.id == taskId && it.revision == taskRevision && control in it.allowedControls
} == true

/** Exact current-task check for non-control intents such as declining approval. */
internal fun TaskRepositoryState.admits(taskId: String, control: TaskControl): Boolean =
    task?.let { it.id == taskId && control in it.allowedControls } == true

private fun TaskConsentIntent.toCorePreview(): TaskConsentPreview = TaskConsentPreview(
    source_hosts = sourceHosts,
    source_discovery_enabled = sourceDiscoveryEnabled,
    new_source_cap = newSourceCap.coerceAtLeast(0).toUInt(),
    provider_route = when (providerRoute) {
        com.taffygo.browser.ui.core.model.ProviderRoute.NOT_CONFIGURED ->
            TaskProviderRoute.NOT_CONFIGURED
        com.taffygo.browser.ui.core.model.ProviderRoute.DIRECT_WITH_YOUR_KEY ->
            TaskProviderRoute.DIRECT_USER_KEY
        com.taffygo.browser.ui.core.model.ProviderRoute.NO_MODEL_REQUIRED ->
            TaskProviderRoute.NO_MODEL_REQUIRED
    },
    // In wire order, so two requests that attach the same stores in a
    // different order are one consent shape on the far side.
    attached_stores = attachedStores.sortedBy { it.ordinal }.map { it.toCore() },
)

private fun TaskAttachedStore.toCore(): CoreTaskAttachedStore = when (this) {
    TaskAttachedStore.HISTORY -> CoreTaskAttachedStore.HISTORY
    TaskAttachedStore.BOOKMARKS -> CoreTaskAttachedStore.BOOKMARKS
    TaskAttachedStore.OPEN_TABS -> CoreTaskAttachedStore.OPEN_TABS
}

/**
 * The followed task with its conversation folded in: the calls that streamed
 * for it, paired to the goal and the follow-ups asked from this process.
 */
private fun TaskRepositoryState.withConversation(
    answer: LiveTaskAnswer?,
    asked: List<AskedQuestion>,
): TaskRepositoryState {
    val current = task ?: return this
    val calls = answer?.takeIf { it.taskId == current.id }?.asOf(generation).orEmpty()
    val questions = asked.filter { it.taskId == current.id }
    val conversation = if (calls.isEmpty() && questions.isEmpty()) {
        null
    } else {
        conversationOf(current.goal, calls, questions)
    }
    val live = conversation?.latestAnswer
    if (current.conversation == conversation && current.liveAnswer == live) return this
    return copy(task = current.copy(liveAnswer = live, conversation = conversation))
}

private fun TaskTemplate.toCoreTemplate(): TaskTemplateId = when (this) {
    TaskTemplate.COMPARE_PRODUCTS -> TaskTemplateId.COMPARE_PRODUCTS
    TaskTemplate.SUMMARIZE_EVIDENCE -> TaskTemplateId.SUMMARIZE_EVIDENCE
    TaskTemplate.BUILD_A_SOURCE_TABLE -> TaskTemplateId.BUILD_SOURCE_TABLE
    TaskTemplate.WEB_ERRAND -> TaskTemplateId.WEB_ERRAND
}

private data class LiveTaskAnswer(
    val taskId: String,
    /** The core generation that streamed these calls (decision 0168). */
    val generation: ULong,
    val calls: List<TaskAnswerProjection>,
)

/**
 * The calls as they can still be true, given the generation now running.
 *
 * A streaming call is a claim that more text is coming. That claim is the
 * speaking generation's to keep, and a generation that has gone cannot keep
 * it: `Reducer::replay` gives up on any call that was in flight and emits no
 * terminal for it, so the last projection this process received stays
 * `isStreaming` and the screen says "Taffy is answering…" for the life of the
 * browser run, with the footer controls hidden behind it.
 *
 * Sealing is keyed to the generation rather than to whether the task is under
 * way, because the task's status and the answer pump are two publications with
 * no ordering between them — an answer's terminal frame is up to one UI frame
 * behind the status that says the task finished, and gating on that race would
 * print "Part of this answer could not be shown." under every completed
 * streamed answer. A generation is a discrete browser-owned fact that moves
 * only when the utility process goes, and when it moves the terminal is
 * genuinely unreachable.
 *
 * A call that had already stopped streaming is left exactly as it was: its
 * text is complete, and the generation it was read in does not change that.
 * Older and not merely different, because the two publications are combined
 * rather than ordered: the answer pump can stamp a generation the projected
 * state has not reached yet, and sealing on that would be the same false
 * caution one frame earlier. `SavedFlowReviewRepository` keys its own
 * residency to the generation the same way; it discards, where this seals,
 * because text a person has already read is not a thing to take off the
 * screen.
 *
 * The list keeps its length. `conversationOf` pairs answers to questions by
 * counting calls, so dropping one would re-pair every answer after it.
 */
private fun LiveTaskAnswer.asOf(generation: ULong): List<TaskAnswerProjection> =
    if (this.generation >= generation) {
        calls
    } else {
        calls.map { call ->
            if (call.isStreaming) call.copy(isStreaming = false, isIncomplete = true) else call
        }
    }

private const val TASK_ANSWER_EVENT_BUFFER = 64

/** Follow-ups kept per process; older marks fall off with the answers they paired to. */
private const val MAX_REMEMBERED_QUESTIONS = 64

/** Ended tasks a person has put away, per process; the oldest falls off at the cap. */
private const val MAX_PUT_AWAY_TASKS = 64
private const val TASK_ANSWER_UI_FRAME_MS = 32L
