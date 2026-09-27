// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.AndroidProfileSecureMaterialStore
import com.taffygo.browser.ui.app.ProviderAccess
import com.taffygo.browser.ui.app.ProviderCredentialCoordinator
import com.taffygo.browser.ui.core.providerauth.ProviderFlowEvent
import com.taffygo.browser.ui.core.providerauth.ProviderFlowEventSink
import org.chromium.taffy.browser.account.mojom.ProviderAccessKind
import org.chromium.taffy.browser.account.mojom.ProviderFlowEvent as MojoProviderFlowEvent
import org.chromium.taffy.browser.account.mojom.ProviderFlowEventKind as MojoProviderFlowEventKind
import org.chromium.taffy.browser.account.mojom.ProviderOauthRecord as MojoProviderOauthRecord
import org.chromium.taffy.browser.account.mojom.TaffyProfilePlatformAdapter
import org.chromium.taffy.core_service.mojom.EffectStatus
import taffy.core_api.ProviderCredentialStateView

/** Provider-credential and sign-in requests behind the profile platform adapter. */
internal class ChromiumProviderPlatformRequests(
    private val secureMaterial: AndroidProfileSecureMaterialStore,
    private val providerCredentials: ProviderCredentialCoordinator,
    private val providerSignInEvents: ProviderFlowEventSink,
    private val requests: ProfilePlatformRequestRunner,
    private val nowEpochMs: () -> Long = System::currentTimeMillis,
) {
    fun resolveProviderCredential(
        providerId: String,
        credentialHandle: String,
        callback: TaffyProfilePlatformAdapter.ResolveProviderCredential_Response,
    ) = requests.launch(block = {
        require(credentialHandle.isNotEmpty()) { "Provider credential handle is empty" }
        require(credentialHandle == providerId) {
            "Provider credential handle is not this store's record name"
        }
        val material = secureMaterial.resolveProviderCredential(providerId)
        try {
            callback.call(
                EffectStatus.COMPLETED,
                org.chromium.taffy.browser.account.mojom.SecretMaterialReadResult().apply {
                    this.material = material
                },
            )
        } finally {
            material.fill(0)
        }
    }, failure = {
        callback.call(it, null)
    })

    fun storeOauthRecord(
        providerId: String,
        record: MojoProviderOauthRecord,
        rotation: Boolean,
        callback: TaffyProfilePlatformAdapter.StoreProviderOauthRecord_Response,
    ) = requests.launch(block = {
        val triple = record.asStoreRecord()
        try {
            // A sign-in completion may land on an empty record and announces
            // the OAUTH credential; a rotation landing on an empty record has
            // raced a forget and is dropped whole (decision 0078).
            val stored = if (rotation) {
                providerCredentials.storeRefreshedRecord(providerId, triple)
            } else {
                providerCredentials.completeSignIn(providerId, triple)
                true
            }
            callback.call(EffectStatus.COMPLETED, stored)
        } finally {
            triple.zero()
        }
    }, failure = {
        record.accessToken?.fill(0)
        record.refreshToken?.fill(0)
        callback.call(it, false)
    })

    fun resolveAccess(
        providerId: String,
        credentialHandle: String,
        callback: TaffyProfilePlatformAdapter.ResolveProviderAccess_Response,
    ) = requests.launch(block = {
        require(credentialHandle.isNotEmpty()) { "Provider credential handle is empty" }
        require(credentialHandle == providerId) {
            "Provider credential handle is not this store's record name"
        }
        when (val access = providerCredentials.resolveAccess(providerId, nowEpochMs())) {
            is ProviderAccess.RawKey ->
                answerAccess(callback, ProviderAccessKind.RAW_KEY, access.material, 0L, null)
            is ProviderAccess.AccessToken ->
                answerAccess(
                    callback,
                    ProviderAccessKind.ACCESS_TOKEN,
                    access.material,
                    access.expiresAtEpochMs,
                    access.credentialHost,
                )
            is ProviderAccess.RefreshRequired ->
                answerAccess(
                    callback,
                    ProviderAccessKind.REFRESH_REQUIRED,
                    access.refreshToken,
                    0L,
                    null,
                )
            ProviderAccess.SignInRequired ->
                answerAccess(callback, ProviderAccessKind.SIGN_IN_REQUIRED, ByteArray(0), 0L, null)
            ProviderAccess.NotConfigured -> callback.call(EffectStatus.COMPLETED, null)
        }
    }, failure = {
        callback.call(it, null)
    })

    fun storeApiKey(
        providerId: String,
        key: ByteArray,
        callback: TaffyProfilePlatformAdapter.StoreProviderApiKey_Response,
    ) = requests.launch(block = {
        try {
            // The pasted key keeps one unchanged path through the coordinator
            // and store; a key the browser minted remains a key.
            providerCredentials.saveApiKey(providerId, key)
            callback.call(EffectStatus.COMPLETED, true)
        } finally {
            key.fill(0)
        }
    }, failure = {
        key.fill(0)
        callback.call(it, false)
    })

    fun onFlowEvent(
        event: MojoProviderFlowEvent,
        callback: TaffyProfilePlatformAdapter.OnProviderFlowEvent_Response,
    ) = requests.launch(block = {
        // Refresh outcomes are credential facts. Other events belong to the
        // visible sign-in engine even when no refresh surface is open.
        when (event.kind) {
            MojoProviderFlowEventKind.REFRESH_SIGN_IN_REQUIRED ->
                providerCredentials.reportCredentialState(
                    event.providerId,
                    ProviderCredentialStateView.NEEDS_SIGN_IN,
                )
            MojoProviderFlowEventKind.REFRESH_FAILED ->
                providerCredentials.reportCredentialState(
                    event.providerId,
                    ProviderCredentialStateView.REFRESH_FAILED,
                )
            else -> providerSignInEvents.onFlowEvent(
                ProviderFlowEvent(
                    providerId = event.providerId,
                    flowId = event.flowId,
                    kind = engineFlowEventKind(event.kind),
                    verificationUrl = event.verificationUrl,
                    userCode = event.userCode,
                ),
            )
        }
        callback.call(EffectStatus.COMPLETED)
    }, failure = {
        callback.call(it)
    })

    private fun answerAccess(
        callback: TaffyProfilePlatformAdapter.ResolveProviderAccess_Response,
        kind: Int,
        material: ByteArray,
        expiresAtEpochMs: Long,
        credentialHost: String?,
    ) {
        try {
            callback.call(
                EffectStatus.COMPLETED,
                providerAccessResult(kind, material, expiresAtEpochMs, credentialHost),
            )
        } finally {
            material.fill(0)
        }
    }
}
