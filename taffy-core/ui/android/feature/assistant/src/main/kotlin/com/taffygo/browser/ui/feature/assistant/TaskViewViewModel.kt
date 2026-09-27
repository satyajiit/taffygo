// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskControlRefusal
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.ui.BrowserRoleOffer
import com.taffygo.browser.ui.core.ui.ReadAloud
import com.taffygo.browser.ui.core.ui.ReadAloudEvent
import com.taffygo.browser.ui.core.ui.ReadAloudSession
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.UnavailableReadAloud
import com.taffygo.browser.ui.core.ui.UnavailableBrowserRoleOffer
import java.util.UUID
import javax.inject.Inject
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

/** Screen SCR-303 state and typed intents over the generated Core API. */
class TaskViewViewModel @Inject constructor(
    private val tasks: TaskRepository,
    private val analytics: AnalyticsClient,
    private val readAloud: ReadAloud = UnavailableReadAloud,
    private val browserRoleOffer: BrowserRoleOffer = UnavailableBrowserRoleOffer,
) : ViewModel() {
    private val dismissedRememberThis = MutableStateFlow(false)
    private val dismissedSetupTaskId = MutableStateFlow<String?>(null)
    private val discardConfirmation = MutableStateFlow<DiscardConfirmation?>(null)
    private val artifactDelivery = MutableStateFlow(ArtifactDeliveryState())
    private val workspaceChange = MutableStateFlow(WorkspaceChangeState())
    private val dismissedControlRefusal = MutableStateFlow<TaskControlRefusal?>(null)
    private val readAloudState = MutableStateFlow<ReadAloudUiState>(ReadAloudUiState.Idle)
    private var readAloudSession: ReadAloudSession? = null
    private var readAloudGeneration = 0L

    val state: StateFlow<TaskViewUiState> = combine(
        combine(tasks.state, dismissedSetupTaskId, workspaceChange, dismissedControlRefusal) {
            repositoryState, dismissedSetup, workspace, dismissedRefusal ->
            RepositoryInputs(repositoryState, dismissedSetup, workspace, dismissedRefusal)
        },
        dismissedRememberThis,
        discardConfirmation,
        artifactDelivery,
        readAloudState,
    ) { inputs, dismissed, confirmation, delivery, playback ->
        val (repositoryState, dismissedSetup, workspace, dismissedRefusal) = inputs
        val projected = projectTaskView(repositoryState)
        val remembered = if (dismissed || projected.state?.isFinal != true) {
            projected.copy(rememberThis = null)
        } else {
            projected
        }
        val task = repositoryState.task
        val pending = delivery.pending?.takeIf { candidate ->
            task != null &&
                candidate.payload.taskId == task.id &&
                task.artifacts.any { artifact ->
                    artifact.id == candidate.artifact.id &&
                        artifact.kind == candidate.artifact.kind &&
                        artifact.accepted
                }
        }
        remembered.copy(
            showDiscardConfirmation = confirmation != null &&
                task != null &&
                task.workspaceId == confirmation.workspaceId &&
                task.workspaceRevision == confirmation.workspaceRevision &&
                task.canDiscardWorkspace,
            artifactExportingId = delivery.exportingArtifactId,
            pendingArtifactExport = pending,
            artifactExportFailed = delivery.failed,
            // A change is still in flight while the workspace it names is
            // still the one this task is offering. The publication that
            // retires the workspace is what clears it.
            workspaceChangeInFlight = workspace.workspaceId != null &&
                workspace.workspaceId == task?.workspaceId &&
                (task.canDiscardWorkspace || task.canSaveWorkspace),
            workspaceChangeRefusal = workspace.refusal,
            // The repository retires it when the revision moves; this only
            // adds the person having read it. The notice explains a button
            // they can still see, so it also goes when the control does.
            controlRefusal = repositoryState.controlRefusal
                ?.takeIf { it != dismissedRefusal && it.control in projected.controls },
            readAloud = playback,
            setupDismissed = dismissedSetup != null && dismissedSetup == task?.id,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = projectTaskView(tasks.state.value),
    )

    init {
        viewModelScope.launch {
            tasks.state
                .map { repositoryState ->
                    val task = repositoryState.task
                    Triple(task?.id, task?.phase, task?.liveAnswer)
                }
                .distinctUntilChanged()
                .drop(1)
                .collect { stopReadAloud() }
        }
    }

    fun onIntent(intent: TaskViewIntent) {
        when (intent) {
            TaskViewIntent.RememberThis,
            TaskViewIntent.DismissRememberThis,
            -> {
                dismissedRememberThis.value = true
                return
            }
            TaskViewIntent.DismissSetup -> {
                dismissedSetupTaskId.value = tasks.state.value.task?.id
                return
            }
            TaskViewIntent.RequestDiscardWorkspace -> {
                val task = tasks.state.value.task
                val workspaceId = task?.workspaceId
                val workspaceRevision = task?.workspaceRevision
                if (task != null && workspaceId != null && workspaceRevision != null &&
                    task.canDiscardWorkspace
                ) {
                    discardConfirmation.value = DiscardConfirmation(workspaceId, workspaceRevision)
                }
                return
            }
            TaskViewIntent.CancelDiscardWorkspace -> {
                discardConfirmation.value = null
                return
            }
            TaskViewIntent.ReadAnswerAloud -> {
                startReadAloud()
                return
            }
            TaskViewIntent.StopReadAloud -> {
                stopReadAloud()
                return
            }
            else -> Unit
        }
        viewModelScope.launch {
            val task = tasks.state.value.task
            when (intent) {
                is TaskViewIntent.Control -> {
                    val rendered = state.value
                    val taskId = rendered.taskId ?: return@launch
                    val taskRevision = rendered.taskRevision ?: return@launch
                    if (intent.control !in rendered.controls) return@launch
                    // The repository records what the browser answered; this
                    // only has to stop hiding it (decision 0221).
                    tasks.useControl(taskId, taskRevision, intent.control)
                }
                TaskViewIntent.ApproveAction -> {
                    val action = task?.pendingAction ?: return@launch
                    tasks.approveAction(task.id, action.id)
                }
                // Declining is fail-closed: cancel the task instead of minting
                // another policy decision in the UI process.
                TaskViewIntent.DenyAction -> task?.let { tasks.cancelTask(it.id) }
                TaskViewIntent.RetryCore -> tasks.retryCore()
                TaskViewIntent.SaveWorkspace -> {
                    val workspaceId = task?.workspaceId ?: return@launch
                    val revision = task.workspaceRevision ?: return@launch
                    if (!task.canSaveWorkspace) return@launch
                    recordWorkspaceChange(
                        workspaceId,
                        tasks.saveWorkspace(workspaceId, revision),
                    )
                }
                TaskViewIntent.ConfirmDiscardWorkspace -> {
                    val confirmation = discardConfirmation.value ?: return@launch
                    discardConfirmation.value = null
                    if (task == null ||
                        task.workspaceId != confirmation.workspaceId ||
                        task.workspaceRevision != confirmation.workspaceRevision ||
                        !task.canDiscardWorkspace
                    ) {
                        return@launch
                    }
                    // The answer is read. It used to be dropped here, so a
                    // refused discard and an admitted one looked the same to
                    // the person: the dialog closed and nothing else moved.
                    recordWorkspaceChange(
                        confirmation.workspaceId,
                        tasks.discardWorkspace(
                            confirmation.workspaceId,
                            confirmation.workspaceRevision,
                        ),
                    )
                }
                TaskViewIntent.DismissControlRefusal ->
                    dismissedControlRefusal.value = tasks.state.value.controlRefusal
                TaskViewIntent.DismissWorkspaceChangeRefusal ->
                    workspaceChange.value = WorkspaceChangeState()
                // Only an ended task. A running one has controls of its own
                // and hiding it would leave work nobody could reach.
                TaskViewIntent.PutTaskAway -> {
                    val ended = task?.takeIf { it.displayState?.isFinal == true } ?: return@launch
                    workspaceChange.value = WorkspaceChangeState()
                    tasks.putAway(ended.id)
                }
                is TaskViewIntent.AcceptArtifact -> {
                    val artifact = task?.artifacts?.firstOrNull {
                        it.id == intent.artifactId && !it.accepted
                    } ?: return@launch
                    val acceptedCommand = tasks.acceptTaskArtifact(task.id, artifact.id)
                    if (acceptedCommand is TaffyResult.Failure) return@launch
                    val accepted = withTimeoutOrNull(ROLE_ACCEPTANCE_TIMEOUT_MILLIS) {
                        tasks.state
                            .map { repository ->
                                repository.task?.takeIf { projected ->
                                    projected.id == task.id &&
                                        projected.qualifiesForBrowserRoleOffer(artifact.id)
                                }
                            }
                            .filterNotNull()
                            .first()
                    }
                    if (accepted != null) browserRoleOffer.offerAfterAcceptedOutput()
                }
                is TaskViewIntent.ExportArtifact -> beginArtifactExport(intent)
                is TaskViewIntent.ArtifactExportHandled -> {
                    val pending = artifactDelivery.value.pending
                    if (pending?.payload?.requestId != intent.requestId) return@launch
                    artifactDelivery.value = ArtifactDeliveryState(failed = !intent.succeeded)
                }
                TaskViewIntent.RequestDiscardWorkspace,
                TaskViewIntent.CancelDiscardWorkspace,
                TaskViewIntent.RememberThis,
                TaskViewIntent.DismissRememberThis,
                TaskViewIntent.DismissSetup,
                TaskViewIntent.ReadAnswerAloud,
                TaskViewIntent.StopReadAloud,
                -> Unit
            }
        }
    }

    /**
     * Records what the core answered about one workspace change.
     *
     * An admitted change is not a finished one — the durable delete lands on a
     * later publication — so success leaves the workspace named and in flight,
     * and only a refusal writes a reason for the surface to say.
     */
    private fun recordWorkspaceChange(workspaceId: String, result: TaffyResult<Unit>) {
        workspaceChange.value = when (result) {
            is TaffyResult.Success -> WorkspaceChangeState(workspaceId = workspaceId)
            is TaffyResult.Failure -> WorkspaceChangeState(refusal = result.reason)
        }
    }

    private fun startReadAloud() {
        stopReadAloud()
        val text = readableTaskAnswer(projectTaskView(tasks.state.value)) ?: return
        val generation = readAloudGeneration
        var terminal = false
        readAloudState.value = ReadAloudUiState.Preparing
        val opened = try {
            readAloud.speak(text) { event ->
                if (readAloudGeneration != generation || terminal) return@speak
                when (event) {
                    ReadAloudEvent.Preparing -> readAloudState.value = ReadAloudUiState.Preparing
                    ReadAloudEvent.Speaking -> readAloudState.value = ReadAloudUiState.Speaking
                    ReadAloudEvent.Finished -> {
                        terminal = true
                        readAloudSession = null
                        readAloudState.value = ReadAloudUiState.Idle
                    }
                    ReadAloudEvent.Failed -> {
                        terminal = true
                        readAloudSession = null
                        readAloudState.value = ReadAloudUiState.Error
                    }
                }
            }
        } catch (_: RuntimeException) {
            readAloudState.value = ReadAloudUiState.Error
            return
        }
        if (readAloudGeneration == generation && !terminal) {
            readAloudSession = opened
        } else {
            opened.close()
        }
    }

    private fun stopReadAloud() {
        readAloudGeneration++
        val active = readAloudSession
        readAloudSession = null
        active?.close()
        readAloudState.value = ReadAloudUiState.Idle
    }

    private suspend fun beginArtifactExport(intent: TaskViewIntent.ExportArtifact) {
        val task = tasks.state.value.task ?: return
        val artifact = task.artifacts.firstOrNull {
            it.id == intent.artifactId && it.accepted
        } ?: return
        val active = artifactDelivery.value
        if (active.exportingArtifactId != null || active.pending != null) return

        val requestId = "task-artifact-${UUID.randomUUID()}"
        artifactDelivery.value = ArtifactDeliveryState(exportingArtifactId = artifact.id)
        val arrival = viewModelScope.async(start = CoroutineStart.UNDISPATCHED) {
            withTimeoutOrNull(ARTIFACT_EXPORT_TIMEOUT_MILLIS) {
                tasks.artifactExports.first { payload ->
                    payload.requestId == requestId &&
                        payload.taskId == task.id &&
                        payload.artifactId == artifact.id &&
                        payload.kind == artifact.kind
                }
            }
        }
        val submitted = tasks.requestTaskArtifactExport(
            requestId = requestId,
            taskId = task.id,
            artifactId = artifact.id,
            kind = artifact.kind,
        )
        if (submitted is TaffyResult.Failure) {
            arrival.cancel()
            artifactDelivery.value = ArtifactDeliveryState(failed = true)
            return
        }
        val payload = arrival.await()
        val current = tasks.state.value.task
        val stillCurrent = current?.id == task.id && current.artifacts.any {
            it.id == artifact.id && it.kind == artifact.kind && it.accepted
        }
        artifactDelivery.value = if (payload != null && stillCurrent) {
            ArtifactDeliveryState(
                exportingArtifactId = artifact.id,
                pending = PendingTaskArtifactExport(artifact, payload, intent.destination),
            )
        } else {
            ArtifactDeliveryState(failed = true)
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.TaskView.screenId))
    }

    override fun onCleared() {
        stopReadAloud()
    }

    private companion object {
        const val ROLE_ACCEPTANCE_TIMEOUT_MILLIS = 5_000L
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val ARTIFACT_EXPORT_TIMEOUT_MILLIS = 30_000L
    }
}

private data class DiscardConfirmation(
    val workspaceId: String,
    val workspaceRevision: ULong,
)

private data class ArtifactDeliveryState(
    val exportingArtifactId: String? = null,
    val pending: PendingTaskArtifactExport? = null,
    val failed: Boolean = false,
)

/**
 * One save or discard the person asked for, between the ask and its answer.
 *
 * `workspaceId` is kept so a change still running is cleared by the state that
 * settles it rather than by the next projection, which would have made the
 * in-flight line flicker off before anything had happened.
 */
/** The four flows the first inner combine carries, named rather than positional. */
private data class RepositoryInputs(
    val repositoryState: TaskRepositoryState,
    val dismissedSetup: String?,
    val workspace: WorkspaceChangeState,
    val dismissedRefusal: TaskControlRefusal?,
)

private data class WorkspaceChangeState(
    val workspaceId: String? = null,
    val refusal: FailureReason? = null,
)
