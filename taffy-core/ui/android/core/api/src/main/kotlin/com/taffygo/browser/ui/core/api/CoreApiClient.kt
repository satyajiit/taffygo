// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import taffy.core_api.AssistantAbilityView
import taffy.core_api.AuthCredentialStatus
import taffy.core_api.AuthProvider
import taffy.core_api.CoreStatus
import taffy.core_api.PermissionDecision
import taffy.core_api.PersonalityPresetView
import taffy.core_api.PlatformPermission
import taffy.core_api.SiteSkillMutationBody

/**
 * Generated-semantics facade from Android UI intent to the browser host.
 *
 * The whole seam is this type; the interfaces it extends are how it is read
 * and implemented in parts, so a repository or a test double narrows to the
 * one it needs. What is declared here is what belongs to no single subject:
 * the profile snapshot, Taffy's own parts, account sign-in, platform
 * permissions, the one assistant configuration, and site skills.
 */
interface CoreApiClient :
    ComposerCoreApiClient,
    LibraryCoreApiClient,
    MemoryCoreApiClient,
    ProviderCoreApiClient,
    SavedDataCoreApiClient,
    SavedFlowCoreApiClient,
    TaskArtifactCoreApiClient,
    TaskLifecycleCoreApiClient,
    WorkspaceCoreApiClient {
    /** Latest immutable profile projection from the browser. */
    val status: StateFlow<CoreStatus>

    /**
     * How far a running download of one of Taffy's own parts has got.
     *
     * A flow rather than a state, and the one thing on this facade that does
     * not come from the isolated core. It cannot: the core hears what a
     * download did once it ends, and a progress bar has to move while one is
     * still running. Nothing is decided from a value here — [status] carries
     * where a download actually got to.
     */
    val assetProgress: Flow<AssetProgressReport>

    suspend fun retryCore()
    suspend fun startAuth(provider: AuthProvider)
    suspend fun requestEmailLink(email: String)
    suspend fun signOut()
    suspend fun deliverCredentialResult(
        flowId: String,
        provider: AuthProvider,
        status: AuthCredentialStatus,
        credentialHandle: String? = null,
    )
    suspend fun deliverPermissionResult(
        requestId: String,
        permission: PlatformPermission,
        decision: PermissionDecision,
    )

    /** Ask for one of Taffy's own parts that start-up would not have fetched. */
    suspend fun requestAsset(assetId: String, assetRevision: String)

    /** Delete one of Taffy's own parts from this device. */
    suspend fun removeAsset(assetId: String, assetRevision: String)

    /**
     * Reads one member out of one installed part of Taffy, by name.
     *
     * The only read on this facade. Everything else here states an intent the
     * isolated core decides on; this asks the browser about a file it already
     * owns, so it answers a verdict rather than an admission status and cannot
     * be refused for staleness.
     */
    suspend fun readPartMember(assetId: String, memberPath: String): PartMemberRead

    /**
     * Replace the one assistant configuration at an exact published revision.
     *
     * Abilities, personality preset and the three presentation scales travel
     * together so two surfaces cannot each save a stale half of the record.
     * The next [status] snapshot, rather than successful submission, is the
     * source of truth for what was durably accepted.
     */
    suspend fun setAssistantConfiguration(
        expectedRevision: ULong,
        disabledAbilities: List<AssistantAbilityView>,
        preset: PersonalityPresetView,
        pace: UInt,
        length: UInt,
        checkIn: UInt,
    )

    /**
     * Teach, revise, review, turn off, or remove one page-specific skill.
     *
     * The body is the generated closed recording vocabulary: semantic-role
     * ordinals, reviewed public HTTPS addresses, compiled tool names, and
     * references to observed/person-supplied values. It carries no private field
     * values, JavaScript, selectors, free-form page text,
     * or an executable payload; the isolated core resolves every verb against
     * its compiled tool registry before it asks storage to commit anything.
     */
    suspend fun mutateSiteSkill(body: SiteSkillMutationBody) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }
}
