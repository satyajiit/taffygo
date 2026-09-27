// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.os.Handler
import android.os.Looper
import com.taffygo.browser.ui.core.api.AssetProgressReport
import com.taffygo.browser.ui.core.api.BrowserCoreApiEndpoint
import com.taffygo.browser.ui.core.api.ComposerCompletionReport
import com.taffygo.browser.ui.core.api.CoreApiEndpointFailure
import com.taffygo.browser.ui.core.api.PartMemberRead
import com.taffygo.browser.ui.core.api.SavedDataCoreApiClient
import com.taffygo.browser.ui.core.api.TaskAnswerReport
import com.taffygo.browser.ui.core.api.TaskArtifactExportReport
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.mojo.bindings.InterfaceRequest
import org.chromium.mojo.system.Pair
import org.chromium.mojo.system.impl.CoreImpl
import org.chromium.taffy.browser.TaffyCoreApiBridge
import org.chromium.taffy.core_api.mojom.CoreApiSubmissionStatus
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApiObserver
import taffy.core_api.AssistantAbilityView
import taffy.core_api.AuthCredentialStatus
import taffy.core_api.AuthProvider
import taffy.core_api.CoreStatus
import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission
import taffy.core_api.PersonalityPresetView
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.TaskTemplateId
import taffy.core_api.TaskArtifactKind
import taffy.core_api.TaskConsentPreview
import taffy.core_api.ThinkingLevelView
import taffy.core_api.WorkspaceExportFormat

/** Profile-lifetime generated-Mojo endpoint; no data class is hand-marshalled over JNI. */
class ChromiumCoreApiEndpoint(profile: Profile) : BrowserCoreApiEndpoint {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val proxy: TaffyProfileCoreApi
    private val snapshots = CoreApiSnapshotLedger()
    private val pushes = CoreApiPushChannels()
    private val transport: CoreApiEndpointTransport
    private val tasks: CoreApiTaskOperations
    private val assets: CoreApiAssetOperations
    private val savedWork: CoreApiSavedWorkOperations
    private val personalization: CoreApiPersonalizationOperations
    private val savedFlows: CoreApiSavedFlowOperations

    override val status: StateFlow<CoreStatus> = snapshots.status
    override val endpointFailure: StateFlow<CoreApiEndpointFailure?> = snapshots.endpointFailure
    override val savedData: SavedDataCoreApiClient get() = savedWork
    override val assetProgress: Flow<AssetProgressReport> = pushes.assetProgress
    override val composerCompletion: Flow<ComposerCompletionReport> = pushes.composerCompletion
    override val taskAnswer: Flow<TaskAnswerReport> = pushes.taskAnswer
    override val taskArtifactExport: Flow<TaskArtifactExportReport> = pushes.taskArtifactExport

    init {
        check(Looper.myLooper() == Looper.getMainLooper()) {
            "The profile Core API endpoint must be created on Chromium's UI thread"
        }
        proxy = TaffyCoreApiBridge.connect(profile)
        transport = CoreApiEndpointTransport(proxy, mainHandler, snapshots)
        tasks = CoreApiTaskOperations(proxy, transport.submissions)
        assets = CoreApiAssetOperations(
            proxy,
            transport.submissions,
            mainHandler,
            transport.closed,
            transport::failTransport,
        )
        savedWork = CoreApiSavedWorkOperations(proxy, transport.submissions)
        personalization = CoreApiPersonalizationOperations(proxy, transport.submissions)
        savedFlows = CoreApiSavedFlowOperations(proxy, transport.submissions)
        val pipe: Pair<TaffyProfileCoreApiObserver.Proxy, InterfaceRequest<TaffyProfileCoreApiObserver>> =
            TaffyProfileCoreApiObserver.MANAGER.getInterfaceRequest(CoreImpl.getInstance())
        val observer = ChromiumCoreApiObserver(
            acceptSnapshot = transport::acceptSnapshot,
            acceptPermissionRequest = transport::acceptPermissionRequest,
            pushes = pushes,
            onInvalidTaskAnswer = {
                snapshots.fail(CoreApiEndpointFailure.EnvelopeMismatch, snapshots.generation)
            },
            isClosed = transport.closed::get,
            onTransportFailure = transport::failTransport,
        )
        transport.attachObserver(TaffyProfileCoreApiObserver.MANAGER.bind(observer, pipe.second))
        proxy.observe(pipe.first)
    }

    override suspend fun startTask(
        goal: String,
        templateId: TaskTemplateId,
        consentPreview: TaskConsentPreview,
        workspaceId: String?,
    ) = tasks.start(goal, templateId, consentPreview, workspaceId)

    override suspend fun startTask(
        goal: String,
        templateId: TaskTemplateId,
        consentPreview: TaskConsentPreview,
        workspaceId: String?,
        skillOfferId: String?,
    ) = tasks.start(goal, templateId, consentPreview, workspaceId, skillOfferId)

    override suspend fun cancelTask(taskId: String) = tasks.cancel(taskId)

    override suspend fun pauseTask(taskId: String) = tasks.pause(taskId)

    override suspend fun resumeTask(taskId: String) =
        tasks.resume(taskId)

    override suspend fun takeOver(taskId: String) =
        tasks.takeOver(taskId)

    override suspend fun completeHandover(taskId: String) =
        tasks.completeHandover(taskId)

    override suspend fun supplyUserInput(taskId: String, answer: String) =
        tasks.supplyUserInput(taskId, answer)

    override suspend fun followUp(taskId: String, question: String) =
        tasks.followUp(taskId, question)

    override suspend fun approveAction(taskId: String, actionId: String) =
        tasks.approveAction(taskId, actionId)

    override suspend fun acceptTaskArtifact(taskId: String, artifactId: String) =
        tasks.acceptArtifact(taskId, artifactId)

    override suspend fun requestTaskArtifactExport(
        requestId: String,
        taskId: String,
        artifactId: String,
        kind: TaskArtifactKind,
    ) = tasks.requestArtifactExport(requestId, taskId, artifactId, kind)

    override suspend fun retryCore() = transport.submissions.submit { callback ->
        proxy.retryCore(status.value.generation.toLong(), callback)
    }

    override suspend fun startAuth(provider: AuthProvider) =
        transport.submissions.submit { callback ->
            proxy.startAuth(provider.wire.toInt(), callback)
        }

    override suspend fun requestEmailLink(email: String) =
        transport.submissions.submit { callback -> proxy.requestEmailLink(email, callback) }

    override suspend fun signOut() =
        transport.submissions.submit { callback -> proxy.signOut(callback) }

    override suspend fun deliverCredentialResult(
        flowId: String,
        provider: AuthProvider,
        status: AuthCredentialStatus,
        credentialHandle: String?,
    ) = transport.submissions.submit { callback ->
        proxy.deliverCredentialResult(
            flowId,
            provider.wire.toInt(),
            status.wire.toInt(),
            credentialHandle,
            callback,
        )
    }

    override suspend fun deliverPermissionResult(
        requestId: String,
        permission: PlatformPermission,
        decision: PermissionDecision,
    ) = transport.submissions.submit { callback ->
        if (!transport.settlePermission(requestId, permission)) {
            callback(CoreApiSubmissionStatus.INVALID_REQUEST)
            return@submit
        }
        proxy.deliverPermissionResult(
            requestId,
            permission.wire.toInt(),
            decision.wire.toInt(),
            callback,
        )
    }

    override suspend fun requestAsset(assetId: String, assetRevision: String) =
        assets.request(assetId, assetRevision)

    override suspend fun removeAsset(assetId: String, assetRevision: String) =
        assets.remove(assetId, assetRevision)

    override suspend fun readPartMember(
        assetId: String,
        memberPath: String,
    ): PartMemberRead = assets.readPartMember(assetId, memberPath)

    override suspend fun correctWorkspaceFact(
        workspaceId: String,
        expectedRevision: ULong,
        factId: String,
        value: String,
    ) = savedWork.correctWorkspaceFact(workspaceId, expectedRevision, factId, value)

    override suspend fun excludeWorkspaceSource(
        workspaceId: String,
        expectedRevision: ULong,
        sourceId: String,
    ) = savedWork.excludeWorkspaceSource(workspaceId, expectedRevision, sourceId)

    override suspend fun requestWorkspaceExport(
        requestId: String,
        workspaceId: String,
        expectedRevision: ULong,
        format: WorkspaceExportFormat,
    ) = savedWork.requestWorkspaceExport(requestId, workspaceId, expectedRevision, format)

    override suspend fun saveWorkspace(workspaceId: String, expectedRevision: ULong) =
        savedWork.saveWorkspace(workspaceId, expectedRevision)

    override suspend fun renameWorkspace(
        workspaceId: String,
        expectedRevision: ULong,
        displayName: String,
    ) = savedWork.renameWorkspace(workspaceId, expectedRevision, displayName)

    override suspend fun deleteWorkspace(
        workspaceId: String,
        expectedRevision: ULong,
        confirmationToken: String,
    ) = savedWork.deleteWorkspace(workspaceId, expectedRevision, confirmationToken)

    override suspend fun discardWorkspace(workspaceId: String, expectedRevision: ULong) =
        savedWork.discardWorkspace(workspaceId, expectedRevision)

    override suspend fun searchLibrary(requestId: String, query: String, limit: UInt) =
        savedWork.searchLibrary(requestId, query, limit)

    override suspend fun startLibraryRefresh(
        previewId: String,
        collectionId: String,
        expectedLibraryRevision: ULong,
        expectedWorkspaceRevision: ULong,
        sourceCount: UInt,
    ) = savedWork.startLibraryRefresh(
        previewId,
        collectionId,
        expectedLibraryRevision,
        expectedWorkspaceRevision,
        sourceCount,
    )

    override suspend fun saveLibraryFact(
        workspaceId: String,
        expectedWorkspaceRevision: ULong,
        factId: String,
        expectedLibraryRevision: ULong,
        expectedEntryRevision: ULong,
    ) = savedWork.saveLibraryFact(
        workspaceId,
        expectedWorkspaceRevision,
        factId,
        expectedLibraryRevision,
        expectedEntryRevision,
    )

    override suspend fun removeLibraryEntry(
        entryId: String,
        expectedLibraryRevision: ULong,
        expectedEntryRevision: ULong,
    ) = savedWork.removeLibraryEntry(entryId, expectedLibraryRevision, expectedEntryRevision)

    override suspend fun requestLibraryExport(
        requestId: String,
        expectedLibraryRevision: ULong,
        collectionId: String?,
        format: WorkspaceExportFormat,
    ) = savedWork.requestLibraryExport(
        requestId,
        expectedLibraryRevision,
        collectionId,
        format,
    )

    override suspend fun searchMemory(requestId: String, query: String, limit: UInt) =
        savedWork.searchMemory(requestId, query, limit)

    override suspend fun upsertMemory(
        memoryId: String?,
        statement: String,
        scopeKind: taffy.core_api.MemoryScopeKind,
        scopeWorkspace: taffy.core_api.MemoryWorkspaceView?,
        sensitivity: taffy.core_api.MemorySensitivity,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
        expiresAtEpochMillis: ULong,
    ) = savedWork.upsertMemory(
        memoryId,
        statement,
        scopeKind,
        scopeWorkspace,
        sensitivity,
        expectedMemoryRevision,
        expectedRecordRevision,
        expiresAtEpochMillis,
    )

    override suspend fun deleteMemory(
        memoryId: String,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
    ) = savedWork.deleteMemory(memoryId, expectedMemoryRevision, expectedRecordRevision)

    override suspend fun saveProviderCredential(
        providerId: String,
        authMethod: ProviderAuthMethodView,
        credentialHandle: String,
    ) = personalization.saveProviderCredential(providerId, authMethod, credentialHandle)

    override suspend fun forgetProviderCredential(providerId: String) =
        personalization.forgetProviderCredential(providerId)

    override suspend fun setProviderCredentialState(
        providerId: String,
        state: ProviderCredentialStateView,
    ) = personalization.setProviderCredentialState(providerId, state)

    override suspend fun probeProviderKey(
        providerId: String,
        credentialHandle: String,
    ) = personalization.probeProviderKey(providerId, credentialHandle)

    override suspend fun startProviderAuth(providerId: String) = personalization.startProviderAuth(providerId)

    override suspend fun cancelProviderAuth(flowId: String) = personalization.cancelProviderAuth(flowId)

    override suspend fun saveCustomProvider(
        providerId: String,
        displayName: String,
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String?,
        models: List<CustomModelSpecView>,
        detectedServer: DetectedServerView?,
    ) = personalization.saveCustomProvider(
        providerId,
        displayName,
        endpoint,
        wireApi,
        credentialHandle,
        models,
        detectedServer,
    )

    override suspend fun removeCustomProvider(providerId: String) =
        personalization.removeCustomProvider(providerId)

    override suspend fun probeCustomEndpoint(
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String?,
        providerId: String,
    ) = personalization.probeCustomEndpoint(endpoint, wireApi, credentialHandle, providerId)

    override suspend fun requestComposerCompletion(
        requestId: String,
        prefix: String,
        suffix: String?,
    ) = personalization.requestComposerCompletion(requestId, prefix, suffix)

    override suspend fun cancelComposerCompletion(requestId: String) =
        personalization.cancelComposerCompletion(requestId)

    override suspend fun setProviderModelPreference(
        providerId: String,
        modelId: String?,
        thinking: ThinkingLevelView?,
    ) = personalization.setProviderModelPreference(providerId, modelId, thinking)

    override suspend fun setAssistantConfiguration(
        expectedRevision: ULong,
        disabledAbilities: List<AssistantAbilityView>,
        preset: PersonalityPresetView,
        pace: UInt,
        length: UInt,
        checkIn: UInt,
    ) = personalization.setAssistantConfiguration(
        expectedRevision,
        disabledAbilities,
        preset,
        pace,
        length,
        checkIn,
    )

    override suspend fun findSavedFlows(requestId: String, goal: String) = savedFlows.find(requestId, goal)

    override suspend fun getSavedFlowReview(requestId: String, skillId: String, expectedVersion: UInt) =
        savedFlows.review(requestId, skillId, expectedVersion)

    override suspend fun openSavedFlowStart(requestId: String, skillId: String, expectedVersion: UInt) =
        savedFlows.open(requestId, skillId, expectedVersion)

    override suspend fun mutateSiteSkill(body: taffy.core_api.SiteSkillMutationBody) =
        personalization.mutateSiteSkill(body)

    override fun close() = transport.close()

    /** Registers one window-owned Android permission surface without exposing task facts. */
    internal fun registerPermissionHandler(
        handler: (String, PlatformPermission) -> Unit,
    ): CoreApiPermissionRegistration = transport.registerPermissionHandler(handler)
}
