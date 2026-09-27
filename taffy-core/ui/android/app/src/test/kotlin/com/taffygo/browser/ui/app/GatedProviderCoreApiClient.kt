// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.api.AssetProgressReport
import com.taffygo.browser.ui.core.api.ComposerCompletionReport
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.PartMemberRead
import kotlinx.coroutines.CompletableDeferred
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
import taffy.core_api.ProviderProbeVerdictView
import taffy.core_api.ProviderProbeView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.TaskConsentPreview
import taffy.core_api.TaskTemplateId
import taffy.core_api.ThinkingLevelView
import taffy.core_api.WorkspaceExportFormat

/**
 * A recording provider-seam fake whose next provider command can be parked on
 * an explicit gate, so a race test can hold one coordinator section open at a
 * known point and prove what a second operation does while it is held.
 *
 * Arm with [gateNext]; the next provider command completes [entered] and then
 * suspends until [release] completes, after which every later call flows
 * straight through. Gates over timing, so the interleaving each test drives
 * is the one it proves.
 */
internal class GatedProviderCoreApiClient : CoreApiClient {
    data class SavedCredential(
        val providerId: String,
        val authMethod: ProviderAuthMethodView,
        val credentialHandle: String,
    )

    data class ReportedState(
        val providerId: String,
        val state: ProviderCredentialStateView,
    )

    data class ProbedKey(
        val providerId: String,
        val credentialHandle: String,
    )

    data class ModelPreference(
        val providerId: String,
        val modelId: String?,
        val thinking: ThinkingLevelView?,
    )

    val saved = mutableListOf<SavedCredential>()
    val forgotten = mutableListOf<String>()
    val reportedStates = mutableListOf<ReportedState>()
    val probed = mutableListOf<ProbedKey>()
    val modelPreferences = mutableListOf<ModelPreference>()

    /**
     * The verdict the "core" publishes right after accepting a probe, or null
     * for a core that never answers. Modelled as a status republication
     * because that is the only channel a real verdict has.
     */
    var probeVerdict: ProviderProbeVerdictView? = null
    private var probeVerdictAtMs = 1_000uL

    /** Completed when the gated command has entered and parked. */
    var entered = CompletableDeferred<Unit>()
        private set

    /** Complete this to let the parked command finish. */
    var release = CompletableDeferred<Unit>()
        private set

    private var gated = false

    /** Arms the gate for the next provider command. */
    fun gateNext() {
        gated = true
        entered = CompletableDeferred()
        release = CompletableDeferred()
    }

    private suspend fun gate() {
        if (!gated) return
        gated = false
        entered.complete(Unit)
        release.await()
    }

    val mutableStatus = MutableStateFlow(
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
            saved_sign_ins = unavailableSavedSignIns(),
            saved_details = unavailableSavedDetails(),
            site_skills = emptyList(),
            builtin_skills = emptyList(),
            projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
            projection_omissions = emptyList(),
        ),
    )

    override val status: StateFlow<CoreStatus> = mutableStatus

    /** Publishes the latest verdict row for [providerId], as the core would. */
    fun publishProbeVerdict(providerId: String, verdict: ProviderProbeVerdictView) {
        probeVerdictAtMs += 1uL
        mutableStatus.value = mutableStatus.value.copy(
            provider_probes = mutableStatus.value.provider_probes
                .filterNot { it.provider_id == providerId } +
                ProviderProbeView(providerId, verdict, probeVerdictAtMs, endpoint = null),
        )
    }

    override suspend fun saveWorkspace(workspaceId: String, expectedRevision: ULong) = Unit

    override suspend fun saveProviderCredential(
        providerId: String,
        authMethod: ProviderAuthMethodView,
        credentialHandle: String,
    ) {
        saved += SavedCredential(providerId, authMethod, credentialHandle)
        gate()
    }

    override suspend fun forgetProviderCredential(providerId: String) {
        forgotten += providerId
        gate()
    }

    override suspend fun setProviderCredentialState(
        providerId: String,
        state: ProviderCredentialStateView,
    ) {
        reportedStates += ReportedState(providerId, state)
        gate()
    }

    override suspend fun probeProviderKey(
        providerId: String,
        credentialHandle: String,
    ) {
        probed += ProbedKey(providerId, credentialHandle)
        probeVerdict?.let { publishProbeVerdict(providerId, it) }
    }

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

private fun unavailableSavedSignIns() = taffy.core_api.SavedSignInsView(
    availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
    revision = 0uL,
    records = emptyList(),
)

private fun unavailableSavedDetails() = taffy.core_api.SavedDetailsView(
    availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
    revision = 0uL,
    people = emptyList(),
)
