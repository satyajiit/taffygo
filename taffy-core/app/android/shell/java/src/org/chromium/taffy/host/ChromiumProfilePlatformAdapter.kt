// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.AndroidProfileSecureMaterialStore
import com.taffygo.browser.ui.app.AuthSurfacePlan
import com.taffygo.browser.ui.app.ProviderCredentialCoordinator
import com.taffygo.browser.ui.core.providerauth.ProviderFlowEventSink
import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CoroutineScope
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.mojo.bindings.Router
import org.chromium.mojo.system.MojoException
import org.chromium.taffy.browser.TaffyProfilePlatformBridge
import org.chromium.taffy.browser.account.mojom.ProviderFlowEvent as MojoProviderFlowEvent
import org.chromium.taffy.browser.account.mojom.ProviderOauthRecord as MojoProviderOauthRecord
import org.chromium.taffy.browser.account.mojom.ResolvedNativeCredentialSurfacePlan
import org.chromium.taffy.browser.account.mojom.ResolvedOAuthSurfacePlan
import org.chromium.taffy.browser.account.mojom.SecretMaterialReadResult
import org.chromium.taffy.browser.account.mojom.SessionMaterialReadRequest
import org.chromium.taffy.browser.account.mojom.SessionMaterialReadResult
import org.chromium.taffy.browser.account.mojom.SessionMaterialMetadata
import org.chromium.taffy.browser.account.mojom.SessionMaterialWriteRequest
import org.chromium.taffy.browser.account.mojom.SessionMaterialWriteResult
import org.chromium.taffy.browser.account.mojom.TaffyProfilePlatformAdapter
import org.chromium.taffy.core_service.mojom.DeletedSecretHandleResult
import org.chromium.taffy.core_service.mojom.EffectStatus
import org.chromium.taffy.core_service.mojom.GenerateEntropyRequest
import org.chromium.taffy.core_service.mojom.GeneratedEntropyResult
import org.chromium.taffy.core_service.mojom.NativeCredentialSurfaceResult
import org.chromium.taffy.core_service.mojom.OAuthSurfaceResult
import org.chromium.taffy.core_service.mojom.TransientSecretWriteResult
import org.chromium.taffy.core_service.mojom.WriteTransientSecretRequest
import org.chromium.taffy.core_service.mojom.DeleteSecretHandleRequest

/** Profile-owned Mojo implementation of Android's narrow platform primitives. */
internal class ChromiumProfilePlatformAdapter(
    profile: Profile,
    private val secureMaterial: AndroidProfileSecureMaterialStore,
    private val surfaces: ProfilePlatformSurfaceDispatcher,
    providerCredentials: ProviderCredentialCoordinator,
    providerSignInEvents: ProviderFlowEventSink,
    scope: CoroutineScope,
) : TaffyProfilePlatformAdapter,
    Closeable {
    private val closed = AtomicBoolean(false)
    private val requests = ProfilePlatformRequestRunner(scope)
    private val providerRequests = ChromiumProviderPlatformRequests(
        secureMaterial,
        providerCredentials,
        providerSignInEvents,
        requests,
    )
    private val router: Router = TaffyProfilePlatformBridge.bindPlatformAdapter(profile, this)

    override fun generateEntropy(
        request: GenerateEntropyRequest,
        callback: TaffyProfilePlatformAdapter.GenerateEntropy_Response,
    ) = launchTerminal(block = {
        val entropy = secureMaterial.generateEntropy(request.byteCount)
        try {
            callback.call(
                EffectStatus.COMPLETED,
                GeneratedEntropyResult().apply {
                    flowId = request.flowId
                    this.entropy = entropy
                },
            )
        } finally {
            entropy.fill(0)
        }
    }, failure = {
        callback.call(it, null)
    })

    override fun writeTransient(
        request: WriteTransientSecretRequest,
        callback: TaffyProfilePlatformAdapter.WriteTransient_Response,
    ) = launchTerminal(block = {
        try {
            val handle = secureMaterial.writeTransient(request.material)
            callback.call(
                EffectStatus.COMPLETED,
                TransientSecretWriteResult().apply {
                    flowId = request.flowId
                    purpose = request.purpose
                    secretHandle = handle
                },
            )
        } finally {
            request.material.fill(0)
        }
    }, failure = {
        request.material.fill(0)
        callback.call(it, null)
    })

    override fun writeAuthorizationCode(
        flowId: String,
        material: ByteArray,
        callback: TaffyProfilePlatformAdapter.WriteAuthorizationCode_Response,
    ) = launchTerminal(block = {
        try {
            val handle = secureMaterial.writeTransient(material)
            callback.call(EffectStatus.COMPLETED, handle)
        } finally {
            material.fill(0)
        }
    }, failure = {
        material.fill(0)
        callback.call(it, null)
    })

    override fun consumeTransient(
        secretHandle: String,
        callback: TaffyProfilePlatformAdapter.ConsumeTransient_Response,
    ) = launchTerminal(block = {
        val material = secureMaterial.consume(secretHandle)
        try {
            callback.call(
                EffectStatus.COMPLETED,
                SecretMaterialReadResult().apply { this.material = material },
            )
        } finally {
            material.fill(0)
        }
    }, failure = {
        callback.call(it, null)
    })

    override fun deleteSecret(
        request: DeleteSecretHandleRequest,
        callback: TaffyProfilePlatformAdapter.DeleteSecret_Response,
    ) = launchTerminal(block = {
        callback.call(
            EffectStatus.COMPLETED,
            DeletedSecretHandleResult().apply {
                secretHandle = request.secretHandle
                deleted = secureMaterial.delete(request.secretHandle)
            },
        )
    }, failure = {
        callback.call(it, null)
    })

    override fun clearTransient(
        callback: TaffyProfilePlatformAdapter.ClearTransient_Response,
    ) = launchTerminal(block = {
        callback.call(EffectStatus.COMPLETED, secureMaterial.clearTransient())
    }, failure = {
        callback.call(it, 0)
    })

    override fun openOAuthSurface(
        plan: ResolvedOAuthSurfacePlan,
        callback: TaffyProfilePlatformAdapter.OpenOAuthSurface_Response,
    ) = launchTerminal(block = {
        val opened = surfaces.openOAuth(AuthSurfacePlan(plan.flowId, plan.authorizationUrl))
        callback.call(
            EffectStatus.COMPLETED,
            OAuthSurfaceResult().apply {
                flowId = plan.flowId
                this.opened = opened
            },
        )
    }, failure = {
        callback.call(it, null)
    })

    override fun openNativeCredentialSurface(
        plan: ResolvedNativeCredentialSurfacePlan,
        callback: TaffyProfilePlatformAdapter.OpenNativeCredentialSurface_Response,
    ) = launchTerminal(block = {
        val opened = surfaces.openGoogleCredential(
            plan.flowId,
            plan.serverClientId,
            plan.hashedNonce,
        )
        callback.call(
            EffectStatus.COMPLETED,
            NativeCredentialSurfaceResult().apply {
                flowId = plan.flowId
                this.opened = opened
            },
        )
    }, failure = {
        callback.call(it, null)
    })

    override fun rotateSession(
        request: SessionMaterialWriteRequest,
        callback: TaffyProfilePlatformAdapter.RotateSession_Response,
    ) = launchTerminal(block = {
        require(request.expectedRotation >= 0L) { "Negative account-session rotation" }
        val result = secureMaterial.rotateSession(
            request.previousHandle,
            request.expectedRotation.toULong(),
            request.material,
        )
        callback.call(
            EffectStatus.COMPLETED,
            SessionMaterialWriteResult().apply {
                sessionHandle = result.handle
                rotation = result.rotation.toLong()
            },
        )
    }, failure = {
        request.material.fill(0)
        callback.call(it, null)
    })

    override fun readSession(
        request: SessionMaterialReadRequest,
        callback: TaffyProfilePlatformAdapter.ReadSession_Response,
    ) = launchTerminal(block = {
        require(request.expectedRotation >= 0L) { "Negative account-session rotation" }
        val material = secureMaterial.readSession(
            request.sessionHandle,
            request.expectedRotation.toULong(),
        )
        try {
            callback.call(
                EffectStatus.COMPLETED,
                SecretMaterialReadResult().apply { this.material = material },
            )
        } finally {
            material.fill(0)
        }
    }, failure = {
        callback.call(it, null)
    })

    override fun deleteSession(
        sessionHandle: String,
        callback: TaffyProfilePlatformAdapter.DeleteSession_Response,
    ) = launchTerminal(block = {
        callback.call(EffectStatus.COMPLETED, secureMaterial.deleteSession(sessionHandle))
    }, failure = {
        callback.call(it, false)
    })

    override fun readCurrentSession(
        sessionHandle: String,
        callback: TaffyProfilePlatformAdapter.ReadCurrentSession_Response,
    ) = launchTerminal(block = {
        val (rotation, material) = secureMaterial.readCurrentSession(sessionHandle)
        try {
            callback.call(
                EffectStatus.COMPLETED,
                SessionMaterialReadResult().apply {
                    this.rotation = rotation.toLong()
                    this.material = material
                },
            )
        } finally {
            material.fill(0)
        }
    }, failure = {
        callback.call(it, null)
    })

    override fun clearSession(
        callback: TaffyProfilePlatformAdapter.ClearSession_Response,
    ) = launchTerminal(block = {
        callback.call(EffectStatus.COMPLETED, secureMaterial.clearSession())
    }, failure = {
        callback.call(it, false)
    })

    override fun inspectCurrentSession(
        callback: TaffyProfilePlatformAdapter.InspectCurrentSession_Response,
    ) = launchTerminal(block = {
        val metadata = secureMaterial.inspectCurrentSession()?.let { session ->
            SessionMaterialMetadata().apply {
                sessionHandle = session.handle
                rotation = session.rotation.toLong()
            }
        }
        callback.call(EffectStatus.COMPLETED, metadata)
    }, failure = {
        callback.call(it, null)
    })

    override fun deleteSessionVersion(
        request: SessionMaterialReadRequest,
        callback: TaffyProfilePlatformAdapter.DeleteSessionVersion_Response,
    ) = launchTerminal(block = {
        require(request.expectedRotation >= 0L) { "Negative account-session rotation" }
        callback.call(
            EffectStatus.COMPLETED,
            secureMaterial.deleteSession(
                request.sessionHandle,
                request.expectedRotation.toULong(),
            ),
        )
    }, failure = {
        callback.call(it, false)
    })

    override fun resolveProviderCredential(
        providerId: String,
        credentialHandle: String,
        callback: TaffyProfilePlatformAdapter.ResolveProviderCredential_Response,
    ) = providerRequests.resolveProviderCredential(providerId, credentialHandle, callback)

    override fun storeProviderOauthRecord(
        providerId: String,
        record: MojoProviderOauthRecord,
        rotation: Boolean,
        callback: TaffyProfilePlatformAdapter.StoreProviderOauthRecord_Response,
    ) = providerRequests.storeOauthRecord(providerId, record, rotation, callback)

    override fun resolveProviderAccess(
        providerId: String,
        credentialHandle: String,
        callback: TaffyProfilePlatformAdapter.ResolveProviderAccess_Response,
    ) = providerRequests.resolveAccess(providerId, credentialHandle, callback)

    override fun storeProviderApiKey(
        providerId: String,
        key: ByteArray,
        callback: TaffyProfilePlatformAdapter.StoreProviderApiKey_Response,
    ) = providerRequests.storeApiKey(providerId, key, callback)

    override fun onProviderFlowEvent(
        event: MojoProviderFlowEvent,
        callback: TaffyProfilePlatformAdapter.OnProviderFlowEvent_Response,
    ) = providerRequests.onFlowEvent(event, callback)

    override fun onConnectionError(error: MojoException) {
        close()
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        runAllTeardownOperations(
            listOf<() -> Unit>(
                { requests.close() },
                { secureMaterial.clearTransient() },
                { surfaces.clear() },
                { router.close() },
            ),
        )
    }

    private fun launchTerminal(
        block: suspend () -> Unit,
        failure: (Int) -> Unit,
    ) = requests.launch(block, failure)
}
