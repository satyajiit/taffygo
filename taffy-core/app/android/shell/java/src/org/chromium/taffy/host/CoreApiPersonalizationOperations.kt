// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.annotation.VisibleForTesting
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi
import taffy.core_api.AssistantAbilityView
import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.PersonalityPresetView
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.SiteSkillMutationBody
import taffy.core_api.ThinkingLevelView
import taffy.core_api.MAX_IDENTIFIER_BYTES

/** Provider, composer, and assistant-configuration submissions over one profile pipe. */
internal class CoreApiPersonalizationOperations(
    private val proxy: TaffyProfileCoreApi,
    private val submissions: CoreApiSubmissionDispatcher,
) {
    // Credential methods carry only the secure-store record's name. Secret
    // material has already stopped at the browser store before this seam.
    suspend fun saveProviderCredential(
        providerId: String,
        authMethod: ProviderAuthMethodView,
        credentialHandle: String,
    ) = submissions.submit { callback ->
        proxy.saveProviderCredential(
            providerId,
            authMethod.wire.toInt(),
            credentialHandle,
            callback,
        )
    }

    suspend fun forgetProviderCredential(providerId: String) =
        submissions.submit { callback -> proxy.forgetProviderCredential(providerId, callback) }

    suspend fun setProviderCredentialState(
        providerId: String,
        state: ProviderCredentialStateView,
    ) = submissions.submit { callback ->
        proxy.setProviderCredentialState(providerId, state.wire.toInt(), callback)
    }

    suspend fun probeProviderKey(providerId: String, credentialHandle: String) =
        submissions.submit { callback ->
            proxy.probeProviderKey(providerId, credentialHandle, callback)
        }

    suspend fun startProviderAuth(providerId: String): String =
        submissions.submitResult(
            call = { callback -> proxy.startProviderAuthFlow(providerId, callback) },
            validateAccepted = ::validatedProviderAuthFlowId,
        )

    suspend fun cancelProviderAuth(flowId: String) =
        submissions.submit { callback -> proxy.cancelProviderAuth(flowId, callback) }

    suspend fun saveCustomProvider(
        providerId: String,
        displayName: String,
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String?,
        models: List<CustomModelSpecView>,
        detectedServer: DetectedServerView?,
    ) = submissions.submit { callback ->
        proxy.saveCustomProvider(
            providerId,
            displayName,
            endpoint,
            wireApi.wire.toInt(),
            credentialHandle,
            models.map { it.toMojo() }.toTypedArray(),
            detectedServer?.toMojo(),
            callback,
        )
    }

    suspend fun removeCustomProvider(providerId: String) =
        submissions.submit { callback -> proxy.removeCustomProvider(providerId, callback) }

    suspend fun probeCustomEndpoint(
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String?,
        providerId: String,
    ) = submissions.submit { callback ->
        proxy.probeCustomEndpoint(
            endpoint,
            wireApi.wire.toInt(),
            credentialHandle,
            providerId,
            callback,
        )
    }

    suspend fun requestComposerCompletion(requestId: String, prefix: String, suffix: String?) =
        submissions.submit { callback ->
            proxy.requestComposerCompletion(requestId, prefix, suffix, callback)
        }

    suspend fun cancelComposerCompletion(requestId: String) =
        submissions.submit { callback -> proxy.cancelComposerCompletion(requestId, callback) }

    suspend fun setProviderModelPreference(
        providerId: String,
        modelId: String?,
        thinking: ThinkingLevelView?,
    ) = submissions.submit { callback ->
        val preference = thinking?.let { rung ->
            org.chromium.taffy.core_api.mojom.ThinkingPreferenceView().apply {
                level = rung.wire.toInt()
            }
        }
        proxy.setProviderModelPreference(providerId, modelId, preference, callback)
    }

    suspend fun setAssistantConfiguration(
        expectedRevision: ULong,
        disabledAbilities: List<AssistantAbilityView>,
        preset: PersonalityPresetView,
        pace: UInt,
        length: UInt,
        checkIn: UInt,
    ) = submissions.submit { callback ->
        proxy.setAssistantConfiguration(
            expectedRevision.toLong(),
            disabledAbilities.map { it.wire.toInt() }.toIntArray(),
            preset.wire.toInt(),
            pace.toInt(),
            length.toInt(),
            checkIn.toInt(),
            callback,
        )
    }

    suspend fun mutateSiteSkill(body: SiteSkillMutationBody) = submissions.submit { callback ->
        proxy.mutateSiteSkill(
            body.kind.wire.toInt(),
            body.skill_id,
            body.expected_version.toInt(),
            body.origin,
            body.clauses.map { clause ->
                org.chromium.taffy.core_api.mojom.SiteSkillObservedClause().apply {
                    kind = clause.kind.wire.toInt()
                    role = clause.role.toInt()
                    detail = clause.detail.toInt()
                }
            }.toTypedArray(),
            body.steps.map { step ->
                org.chromium.taffy.core_api.mojom.SiteSkillObservedStep().apply {
                    verb = step.verb
                    arguments = step.arguments.map { argument ->
                        org.chromium.taffy.core_api.mojom.SiteSkillObservedArgument().apply {
                            parameter = argument.parameter.toInt()
                            kind = argument.kind.wire.toInt()
                            value = argument.value.toLong()
                            purpose = argument.purpose.toInt()
                        }
                    }.toTypedArray()
                    postcondition = step.postcondition.toInt()
                    hasFill = step.has_fill
                    fillPurpose = step.fill_purpose.toInt()
                }
            }.toTypedArray(),
            body.admitted.toInt(),
            body.enabled,
            callback,
        )
    }
}

/** Accept only a complete identifier from an accepted browser start reply. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun validatedProviderAuthFlowId(flowId: String?): String? =
    flowId?.takeIf {
        it.isNotEmpty() && it.encodeToByteArray().size <= MAX_IDENTIFIER_BYTES
    }
