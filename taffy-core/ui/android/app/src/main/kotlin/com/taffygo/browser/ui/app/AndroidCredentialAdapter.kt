// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.app.Activity
import androidx.credentials.CredentialManager
import androidx.credentials.CustomCredential
import androidx.credentials.GetCredentialRequest
import androidx.credentials.exceptions.GetCredentialCancellationException
import androidx.credentials.exceptions.NoCredentialException
import com.google.android.libraries.identity.googleid.GetGoogleIdOption
import com.google.android.libraries.identity.googleid.GoogleIdTokenCredential
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import javax.inject.Inject
import taffy.core_api.AuthCredentialStatus
import taffy.core_api.AuthProvider

/**
 * Android's visible credential surface.
 *
 * The public nonce hash and client identifier arrive from the browser-owned
 * auth effect; the corresponding raw nonce never enters Android. Credential
 * material is handed to the Core API immediately and never enters Compose
 * state or logs.
 */
@TaffyWindowScope
class AndroidCredentialAdapter @Inject constructor(
    private val activity: Activity,
    private val core: CoreApiClient,
    private val handles: CredentialHandleBroker,
) : CredentialEndpoint {
    override suspend fun requestGoogleCredential(
        flowId: String,
        serverClientId: String,
        hashedNonce: String,
    ) {
        require(flowId.isNotEmpty() && flowId.encodeToByteArray().size <= MAX_IDENTIFIER_BYTES) {
            "Credential flow identifier is empty or exceeds its bound"
        }
        require(
            serverClientId.isNotEmpty() &&
                serverClientId.encodeToByteArray().size <= MAX_SERVER_CLIENT_ID_BYTES,
        ) { "Credential server client identifier is empty or exceeds its bound" }
        require(hashedNonce.isValidGoogleNonceHash()) {
            "Credential nonce hash must be lowercase SHA-256 hex"
        }
        val option = GetGoogleIdOption.Builder()
            .setServerClientId(serverClientId)
            .setFilterByAuthorizedAccounts(false)
            .setNonce(hashedNonce)
            .build()
        val request = GetCredentialRequest.Builder().addCredentialOption(option).build()
        val terminal = try {
            val credential = CredentialManager.create(activity)
                .getCredential(activity, request)
                .credential
            if (credential !is CustomCredential ||
                credential.type != GoogleIdTokenCredential.TYPE_GOOGLE_ID_TOKEN_CREDENTIAL
            ) {
                CredentialTerminal(AuthCredentialStatus.NO_CREDENTIAL)
            } else {
                val bytes = GoogleIdTokenCredential.createFrom(credential.data)
                    .idToken
                    .encodeToByteArray()
                try {
                    CredentialTerminal(
                        status = AuthCredentialStatus.SUCCESS,
                        handle = handles.store(bytes),
                    )
                } finally {
                    bytes.fill(0)
                }
            }
        } catch (_: GetCredentialCancellationException) {
            CredentialTerminal(AuthCredentialStatus.CANCELLED)
        } catch (_: NoCredentialException) {
            CredentialTerminal(AuthCredentialStatus.NO_CREDENTIAL)
        } catch (_: Exception) {
            CredentialTerminal(AuthCredentialStatus.UNAVAILABLE)
        }
        core.deliverCredentialResult(
            flowId = flowId,
            provider = AuthProvider.GOOGLE,
            status = terminal.status,
            credentialHandle = terminal.handle,
        )
    }

    private data class CredentialTerminal(
        val status: AuthCredentialStatus,
        val handle: String? = null,
    )

    private companion object {
        const val MAX_IDENTIFIER_BYTES = 256
        const val MAX_SERVER_CLIENT_ID_BYTES = 512
    }
}
