// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.api.AssetProgressReport
import com.taffygo.browser.ui.core.api.ComposerCompletionReport
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import com.taffygo.browser.ui.core.api.PartMemberRead
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.emptyFlow
import taffy.core_api.AuthCredentialStatus
import taffy.core_api.AuthProvider
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.PartMemberStatus
import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.TaskConsentPreview
import taffy.core_api.TaskTemplateId
import taffy.core_api.ThinkingLevelView
import taffy.core_api.WorkspaceExportFormat

/** A complete [CoreApiClient] that records the provider seam and answers nothing else. */
internal class RecordingProviderCoreApiClient(
    private val refuse: Boolean = false,
) : CoreApiClient {
    data class SavedCredential(
        val providerId: String,
        val authMethod: ProviderAuthMethodView,
        val credentialHandle: String,
    )

    data class ReportedState(
        val providerId: String,
        val state: ProviderCredentialStateView,
    )

    data class ModelPreference(
        val providerId: String,
        val modelId: String?,
        val thinking: ThinkingLevelView?,
    )

    val saved = mutableListOf<SavedCredential>()
    val forgotten = mutableListOf<String>()
    val reportedStates = mutableListOf<ReportedState>()
    val modelPreferences = mutableListOf<ModelPreference>()

    override val status: StateFlow<CoreStatus> = MutableStateFlow(
        CoreStatus(
            availability = CoreAvailability.READY,
            generation = 1uL,
            active_tasks = emptyList(),
            auth_state = null,
            workspaces = emptyList(),
            workspace_export = null,
            asset_delivery = null,
            provider_roster = emptyList(),
            provider_probes = emptyList(),
            provider_models = emptyList(),
            library = taffy.core_api.LibraryViewState(
                availability = taffy.core_api.LibraryAvailability.AVAILABLE,
                revision = 0uL,
                entries = emptyList(),
                search = null,
                refresh_previews = emptyList(),
                refresh_results = emptyList(),
            ),
            library_export = null,
            memory = taffy.core_api.MemoryViewState(
                availability = taffy.core_api.MemoryAvailability.AVAILABLE,
                revision = 0uL,
                records = emptyList(),
                search = null,
            ),
            assistant_configuration = taffy.core_api.AssistantConfigurationView(
                revision = 0uL,
                disabled_abilities = emptyList(),
                preset = taffy.core_api.PersonalityPresetView.CAREFUL_RESEARCHER,
                pace = 0u,
                length = 1u,
                check_in = 0u,
            ),
            saved_sign_ins = taffy.core_api.SavedSignInsView(
                availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
                revision = 0uL,
                records = emptyList(),
            ),
            saved_details = taffy.core_api.SavedDetailsView(
                availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
                revision = 0uL,
                people = emptyList(),
            ),
            site_skills = emptyList(),
            builtin_skills = emptyList(),
            projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
            projection_omissions = emptyList(),
        ),
    )

    override suspend fun saveWorkspace(workspaceId: String, expectedRevision: ULong) = Unit

    override suspend fun saveProviderCredential(
        providerId: String,
        authMethod: ProviderAuthMethodView,
        credentialHandle: String,
    ) {
        if (refuse) {
            throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
        }
        saved += SavedCredential(providerId, authMethod, credentialHandle)
    }

    override suspend fun forgetProviderCredential(providerId: String) {
        if (refuse) {
            throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
        }
        forgotten += providerId
    }

    override suspend fun setProviderCredentialState(
        providerId: String,
        state: ProviderCredentialStateView,
    ) {
        if (refuse) {
            throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
        }
        reportedStates += ReportedState(providerId, state)
    }

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
    ) {
        if (refuse) {
            throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
        }
        modelPreferences += ModelPreference(providerId, modelId, thinking)
    }

    // Nothing below is this fake's subject; the members exist so it is a
    // complete CoreApiClient.
    override val assetProgress: Flow<AssetProgressReport> = emptyFlow()
    override val composerCompletion: Flow<ComposerCompletionReport> = emptyFlow()

    override suspend fun requestComposerCompletion(
        requestId: String,
        prefix: String,
        suffix: String?,
    ) = Unit

    override suspend fun cancelComposerCompletion(requestId: String) = Unit

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

    override suspend fun requestWorkspaceExport(
        requestId: String,
        workspaceId: String,
        expectedRevision: ULong,
        format: WorkspaceExportFormat,
    ) = Unit

    override suspend fun requestAsset(assetId: String, assetRevision: String) = Unit
    override suspend fun removeAsset(assetId: String, assetRevision: String) = Unit

    override suspend fun readPartMember(assetId: String, memberPath: String) =
        PartMemberRead(PartMemberStatus.NOT_INSTALLED, null)

    override suspend fun setAssistantConfiguration(
        expectedRevision: ULong,
        disabledAbilities: List<taffy.core_api.AssistantAbilityView>,
        preset: taffy.core_api.PersonalityPresetView,
        pace: UInt,
        length: UInt,
        checkIn: UInt,
    ) = Unit
}
