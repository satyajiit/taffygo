// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.workspace.internal

import com.taffygo.browser.ui.core.api.AssetProgressReport
import com.taffygo.browser.ui.core.api.ComposerCompletionReport
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.PartMemberRead
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import taffy.core_api.PartMemberStatus
import taffy.core_api.AuthCredentialStatus
import taffy.core_api.AuthProvider
import taffy.core_api.CoreStatus
import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.TaskTemplateId
import taffy.core_api.ThinkingLevelView
import taffy.core_api.WorkspaceExportFormat

internal class RecordingWorkspaceCoreApiClient(initial: CoreStatus) : CoreApiClient {
    private val mutableStatus = MutableStateFlow(initial)
    override val status: StateFlow<CoreStatus> = mutableStatus

    var correction: Correction? = null
    var exclusion: Exclusion? = null
    var export: Export? = null
    var rename: Rename? = null
    var deletion: Deletion? = null

    fun publish(value: CoreStatus) {
        mutableStatus.value = value
    }

    override suspend fun correctWorkspaceFact(
        workspaceId: String,
        expectedRevision: ULong,
        factId: String,
        value: String,
    ) {
        correction = Correction(workspaceId, expectedRevision, factId, value)
    }

    override suspend fun excludeWorkspaceSource(
        workspaceId: String,
        expectedRevision: ULong,
        sourceId: String,
    ) {
        exclusion = Exclusion(workspaceId, expectedRevision, sourceId)
    }

    override suspend fun requestWorkspaceExport(
        requestId: String,
        workspaceId: String,
        expectedRevision: ULong,
        format: WorkspaceExportFormat,
    ) {
        export = Export(requestId, workspaceId, expectedRevision, format)
    }

    override suspend fun renameWorkspace(
        workspaceId: String,
        expectedRevision: ULong,
        displayName: String,
    ) {
        rename = Rename(workspaceId, expectedRevision, displayName)
    }

    override suspend fun deleteWorkspace(
        workspaceId: String,
        expectedRevision: ULong,
        confirmationToken: String,
    ) {
        deletion = Deletion(workspaceId, expectedRevision, confirmationToken)
    }

    override suspend fun startTask(
        goal: String,
        templateId: TaskTemplateId,
        consentPreview: taffy.core_api.TaskConsentPreview,
        workspaceId: String?,
    ) = Unit

    override suspend fun cancelTask(taskId: String) = Unit

    override suspend fun pauseTask(taskId: String) = Unit

    override suspend fun resumeTask(taskId: String) = Unit

    override suspend fun takeOver(taskId: String) = Unit
    override suspend fun completeHandover(taskId: String) = Unit
    override suspend fun supplyUserInput(taskId: String, answer: String) = Unit
    override suspend fun followUp(taskId: String, question: String) = Unit
    override suspend fun approveAction(taskId: String, actionId: String) = Unit
    override suspend fun retryCore() = Unit
    override suspend fun startAuth(provider: AuthProvider) = Unit
    override suspend fun requestEmailLink(email: String) = Unit
    override suspend fun signOut() = Unit
    override suspend fun deliverCredentialResult(
        flowId: String,
        provider: AuthProvider,
        status: AuthCredentialStatus,
        credentialHandle: String?,
    ) = Unit

    override suspend fun deliverPermissionResult(
        requestId: String,
        permission: PlatformPermission,
        decision: PermissionDecision,
    ) = Unit

    // Delivery is not this module's subject; the members exist so the fake is
    // a complete CoreApiClient and answer nothing.
    override val assetProgress: Flow<AssetProgressReport> = emptyFlow()
    override val composerCompletion: Flow<ComposerCompletionReport> = emptyFlow()

    override suspend fun requestComposerCompletion(
        requestId: String,
        prefix: String,
        suffix: String?,
    ) = Unit

    override suspend fun cancelComposerCompletion(requestId: String) = Unit

    override suspend fun requestAsset(assetId: String, assetRevision: String) = Unit

    override suspend fun removeAsset(assetId: String, assetRevision: String) = Unit

    override suspend fun readPartMember(assetId: String, memberPath: String) =
        PartMemberRead(PartMemberStatus.NOT_INSTALLED, null)

    // The provider seam is not this module's subject; the members exist so the
    // fake is a complete CoreApiClient and record nothing.
    override suspend fun saveWorkspace(workspaceId: String, expectedRevision: ULong) = Unit

    override suspend fun saveProviderCredential(
        providerId: String,
        authMethod: ProviderAuthMethodView,
        credentialHandle: String,
    ) = Unit

    override suspend fun forgetProviderCredential(providerId: String) = Unit

    override suspend fun setProviderCredentialState(
        providerId: String,
        state: ProviderCredentialStateView,
    ) = Unit

    override suspend fun probeProviderKey(
        providerId: String,
        credentialHandle: String,
    ) = Unit

    override suspend fun startProviderAuth(providerId: String): String = "provider-flow-test"

    override suspend fun cancelProviderAuth(flowId: String) = Unit

    override suspend fun saveCustomProvider(
        providerId: String,
        displayName: String,
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String?,
        models: List<CustomModelSpecView>,
        detectedServer: DetectedServerView?,
    ) = Unit

    override suspend fun probeCustomEndpoint(
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String?,
        providerId: String,
    ) = Unit

    override suspend fun removeCustomProvider(providerId: String) = Unit

    override suspend fun setProviderModelPreference(
        providerId: String,
        modelId: String?,
        thinking: ThinkingLevelView?,
    ) = Unit

    override suspend fun setAssistantConfiguration(
        expectedRevision: ULong,
        disabledAbilities: List<taffy.core_api.AssistantAbilityView>,
        preset: taffy.core_api.PersonalityPresetView,
        pace: UInt,
        length: UInt,
        checkIn: UInt,
    ) = Unit

    data class Correction(
        val workspaceId: String,
        val revision: ULong,
        val factId: String,
        val value: String,
    )

    data class Exclusion(
        val workspaceId: String,
        val revision: ULong,
        val sourceId: String,
    )

    data class Export(
        val requestId: String,
        val workspaceId: String,
        val revision: ULong,
        val format: WorkspaceExportFormat,
    )

    data class Rename(
        val workspaceId: String,
        val revision: ULong,
        val displayName: String,
    )

    data class Deletion(
        val workspaceId: String,
        val revision: ULong,
        val confirmationToken: String,
    )
}
