// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.api.AssetProgressReport
import com.taffygo.browser.ui.core.api.ComposerCompletionReport
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.PartMemberRead
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.emptyFlow
import taffy.core_api.AssistantAbilityView
import taffy.core_api.AuthCredentialStatus
import taffy.core_api.AuthProvider
import taffy.core_api.CoreStatus
import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.MemoryScopeKind
import taffy.core_api.MemorySensitivity
import taffy.core_api.MemoryWorkspaceView
import taffy.core_api.PartMemberStatus
import taffy.core_api.PermissionDecision
import taffy.core_api.PersonalityPresetView
import taffy.core_api.PlatformPermission
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.SiteSkillMutationBody
import taffy.core_api.TaskConsentPreview
import taffy.core_api.TaskTemplateId
import taffy.core_api.ThinkingLevelView
import taffy.core_api.WorkspaceExportFormat

/** Complete Core API fake recording the settings-owned command families. */
internal class RecordingAssistantConfigurationCoreApiClient(initial: CoreStatus) : CoreApiClient {
    private val mutableStatus = MutableStateFlow(initial)
    override val status: StateFlow<CoreStatus> = mutableStatus
    override val assetProgress: Flow<AssetProgressReport> = emptyFlow()
    override val composerCompletion: Flow<ComposerCompletionReport> = emptyFlow()

    val writes = mutableListOf<Write>()
    val memoryUpserts = mutableListOf<MemoryUpsert>()
    val memoryDeletions = mutableListOf<MemoryDeletion>()
    val memorySearches = mutableListOf<MemorySearch>()
    val savedDetailUpserts = mutableListOf<SavedDetailUpsert>()
    val savedDetailDeletions = mutableListOf<SavedDataDeletion>()
    val savedSignInDeletions = mutableListOf<SavedDataDeletion>()
    val skillMutations = mutableListOf<SiteSkillMutationBody>()

    fun publish(value: CoreStatus) {
        mutableStatus.value = value
    }

    override suspend fun setAssistantConfiguration(
        expectedRevision: ULong,
        disabledAbilities: List<AssistantAbilityView>,
        preset: PersonalityPresetView,
        pace: UInt,
        length: UInt,
        checkIn: UInt,
    ) {
        writes += Write(
            expectedRevision,
            disabledAbilities,
            preset,
            pace,
            length,
            checkIn,
        )
    }

    override suspend fun mutateSiteSkill(body: SiteSkillMutationBody) {
        skillMutations += body
    }

    override suspend fun startTask(
        goal: String,
        templateId: TaskTemplateId,
        consentPreview: TaskConsentPreview,
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

    override suspend fun correctWorkspaceFact(
        workspaceId: String,
        expectedRevision: ULong,
        factId: String,
        value: String,
    ) = Unit

    override suspend fun excludeWorkspaceSource(
        workspaceId: String,
        expectedRevision: ULong,
        sourceId: String,
    ) = Unit

    override suspend fun requestAsset(assetId: String, assetRevision: String) = Unit
    override suspend fun removeAsset(assetId: String, assetRevision: String) = Unit

    override suspend fun readPartMember(assetId: String, memberPath: String) =
        PartMemberRead(PartMemberStatus.NOT_INSTALLED, null)

    override suspend fun requestWorkspaceExport(
        requestId: String,
        workspaceId: String,
        expectedRevision: ULong,
        format: WorkspaceExportFormat,
    ) = Unit

    override suspend fun saveWorkspace(workspaceId: String, expectedRevision: ULong) = Unit

    override suspend fun searchMemory(requestId: String, query: String, limit: UInt) {
        memorySearches += MemorySearch(requestId, query, limit)
    }

    override suspend fun upsertMemory(
        memoryId: String?,
        statement: String,
        scopeKind: MemoryScopeKind,
        scopeWorkspace: MemoryWorkspaceView?,
        sensitivity: MemorySensitivity,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
        expiresAtEpochMillis: ULong,
    ) {
        memoryUpserts += MemoryUpsert(
            memoryId = memoryId,
            statement = statement,
            scopeKind = scopeKind,
            scopeWorkspace = scopeWorkspace,
            sensitivity = sensitivity,
            expectedMemoryRevision = expectedMemoryRevision,
            expectedRecordRevision = expectedRecordRevision,
            expiresAtEpochMillis = expiresAtEpochMillis,
        )
    }

    override suspend fun deleteMemory(
        memoryId: String,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
    ) {
        memoryDeletions += MemoryDeletion(
            memoryId = memoryId,
            expectedMemoryRevision = expectedMemoryRevision,
            expectedRecordRevision = expectedRecordRevision,
        )
    }

    override suspend fun upsertSavedDetail(
        expectedRevision: ULong,
        detailId: String?,
        givenName: String,
        familyName: String,
        email: String,
        phone: String,
        address: String,
        postcode: String,
        country: String,
    ) {
        savedDetailUpserts += SavedDetailUpsert(
            expectedRevision,
            detailId,
            givenName,
            familyName,
            email,
            phone,
            address,
            postcode,
            country,
        )
    }

    override suspend fun deleteSavedDetail(detailId: String, expectedRevision: ULong) {
        savedDetailDeletions += SavedDataDeletion(detailId, expectedRevision)
    }

    override suspend fun deleteSavedSignIn(signInId: String, expectedRevision: ULong) {
        savedSignInDeletions += SavedDataDeletion(signInId, expectedRevision)
    }

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

    override suspend fun probeProviderKey(providerId: String, credentialHandle: String) = Unit

    override suspend fun probeCustomEndpoint(
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String?,
        providerId: String,
    ) = Unit

    override suspend fun requestComposerCompletion(
        requestId: String,
        prefix: String,
        suffix: String?,
    ) = Unit

    override suspend fun cancelComposerCompletion(requestId: String) = Unit
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

    override suspend fun removeCustomProvider(providerId: String) = Unit

    override suspend fun setProviderModelPreference(
        providerId: String,
        modelId: String?,
        thinking: ThinkingLevelView?,
    ) = Unit

    data class Write(
        val expectedRevision: ULong,
        val disabledAbilities: List<AssistantAbilityView>,
        val preset: PersonalityPresetView,
        val pace: UInt,
        val length: UInt,
        val checkIn: UInt,
    )

    data class MemoryUpsert(
        val memoryId: String?,
        val statement: String,
        val scopeKind: MemoryScopeKind,
        val scopeWorkspace: MemoryWorkspaceView?,
        val sensitivity: MemorySensitivity,
        val expectedMemoryRevision: ULong,
        val expectedRecordRevision: ULong,
        val expiresAtEpochMillis: ULong,
    )

    data class MemoryDeletion(
        val memoryId: String,
        val expectedMemoryRevision: ULong,
        val expectedRecordRevision: ULong,
    )

    data class MemorySearch(
        val requestId: String,
        val query: String,
        val limit: UInt,
    )

    data class SavedDetailUpsert(
        val expectedRevision: ULong,
        val detailId: String?,
        val givenName: String,
        val familyName: String,
        val email: String,
        val phone: String,
        val address: String,
        val postcode: String,
        val country: String,
    )

    data class SavedDataDeletion(val id: String, val expectedRevision: ULong)
}
