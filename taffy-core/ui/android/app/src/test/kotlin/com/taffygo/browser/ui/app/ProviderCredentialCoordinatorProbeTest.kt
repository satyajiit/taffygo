// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.credentials.ProviderProbeVerdict
import java.security.SecureRandom
import java.util.Base64
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderProbeVerdictView

/**
 * The coordinator's probe leg (decision 0083): a pasted draft is sealed as a
 * one-shot transient, only its opaque handle crosses to the core, and the
 * verdict is read off the status snapshots the core publishes — never off a
 * row an earlier probe left behind. The fake core answers by republishing
 * status, because that is the only channel a real verdict has.
 * Explicit secret-box gates also pin projection changes inside the two secure
 * store suspension points so cleanup is proved without scheduler timing.
 */
class ProviderCredentialCoordinatorProbeTest {

    @Test
    fun `a recovery projection refuses a key probe before dispatch`() = runTest {
        val store = store()
        val coreApi = GatedProviderCoreApiClient()
        coreApi.mutableStatus.value = coreApi.mutableStatus.value.copy(
            projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
        )
        val coordinator = coordinator(store, coreApi)

        val verdict = coordinator.probeApiKey("anthropic", "key".encodeToByteArray())

        assertEquals(ProviderProbeVerdict.UNKNOWN, verdict)
        assertTrue(coreApi.probed.isEmpty())
        assertTrue(store.configuredProviderIds().isEmpty())
    }

    @Test
    fun `a recovery projection refuses a key save before storage or dispatch`() = runTest {
        val store = store()
        val coreApi = GatedProviderCoreApiClient()
        coreApi.mutableStatus.value = coreApi.mutableStatus.value.copy(
            projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
        )
        val coordinator = coordinator(store, coreApi)

        val failure = runCatching {
            coordinator.saveApiKey("anthropic", "key".encodeToByteArray())
        }.exceptionOrNull()

        assertTrue(failure is IllegalStateException)
        assertTrue(coreApi.saved.isEmpty())
        assertTrue(store.configuredProviderIds().isEmpty())
    }

    @Test
    fun `recovery reached while a transient is sealing deletes it before probe dispatch`() =
        runTest {
            val secretBox = GatedSecretBox()
            val store = store(secretBox)
            val coreApi = GatedProviderCoreApiClient().apply {
                probeVerdict = ProviderProbeVerdictView.USABLE
            }
            val coordinator = coordinator(store, coreApi)

            val probe = async {
                coordinator.probeApiKey("anthropic", "key".encodeToByteArray())
            }
            secretBox.entered.await()
            coreApi.mutableStatus.value = coreApi.mutableStatus.value.copy(
                projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
            )
            secretBox.release.complete(Unit)

            assertEquals(ProviderProbeVerdict.UNKNOWN, probe.await())
            assertTrue(coreApi.probed.isEmpty())
            assertEquals(0, store.clearTransient())
        }

    @Test
    fun `recovery reached while an api key is sealing rolls it back before dispatch`() =
        runTest {
            val secretBox = GatedSecretBox()
            val store = store(secretBox)
            val coreApi = GatedProviderCoreApiClient()
            val coordinator = coordinator(store, coreApi)

            val save = async {
                runCatching {
                    coordinator.saveApiKey("anthropic", "key".encodeToByteArray())
                }.exceptionOrNull()
            }
            secretBox.entered.await()
            coreApi.mutableStatus.value = coreApi.mutableStatus.value.copy(
                projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
            )
            secretBox.release.complete(Unit)

            assertTrue(save.await() is IllegalStateException)
            assertTrue(coreApi.saved.isEmpty())
            assertTrue(store.configuredProviderIds().isEmpty())
            assertTrue(coordinator.configuredProviderIds.value.isEmpty())
        }

    @Test
    fun `an oauth completion still converges if recovery arrives while it is sealing`() =
        runTest {
            val secretBox = GatedSecretBox()
            val store = store(secretBox)
            val coreApi = GatedProviderCoreApiClient()
            val coordinator = coordinator(store, coreApi)

            val completion = async {
                coordinator.completeSignIn(
                    "xai",
                    ProviderOauthRecord(
                        tokenType = "Bearer",
                        expiresAtEpochMs = 10_000,
                        scopes = "offline_access",
                        accessToken = "access-not-a-real-token".encodeToByteArray(),
                        refreshToken = "refresh-not-a-real-token".encodeToByteArray(),
                    ),
                )
            }
            secretBox.entered.await()
            coreApi.mutableStatus.value = coreApi.mutableStatus.value.copy(
                projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
            )
            secretBox.release.complete(Unit)
            completion.await()

            assertEquals(
                listOf(
                    GatedProviderCoreApiClient.SavedCredential(
                        providerId = "xai",
                        authMethod = ProviderAuthMethodView.OAUTH,
                        credentialHandle = "xai",
                    ),
                ),
                coreApi.saved,
            )
            assertEquals(setOf("xai"), store.configuredProviderIds())
            assertEquals(setOf("xai"), coordinator.configuredProviderIds.value)
        }

    @Test
    fun `a probe seals the draft as a transient and hands the core only its handle`() = runTest {
        val store = store()
        val coreApi = GatedProviderCoreApiClient()
        coreApi.probeVerdict = ProviderProbeVerdictView.USABLE
        val coordinator = coordinator(store, coreApi)
        val draft = "sk-not-a-real-key".encodeToByteArray()

        val verdict = coordinator.probeApiKey("anthropic", draft)

        assertEquals(ProviderProbeVerdict.USABLE, verdict)
        assertEquals(1, coreApi.probed.size)
        val handle = coreApi.probed.first().credentialHandle
        // The handle is opaque, not the material or the provider id.
        assertFalse(handle.contains("sk-not-a-real-key"))
        assertFalse(handle == "anthropic")
        // What the vault holds under it is exactly the draft, spendable once.
        assertEquals("sk-not-a-real-key", store.consume(handle).decodeToString())
        // The caller keeps ownership: the probe worked on a copy, so a usable
        // verdict can still be followed by saving these same bytes.
        assertEquals("sk-not-a-real-key", draft.decodeToString())
    }

    @Test
    fun `a definitive refusal comes back as the verdict the core filed`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        coreApi.probeVerdict = ProviderProbeVerdictView.AUTH
        val coordinator = coordinator(store(), coreApi)

        val verdict = coordinator.probeApiKey("anthropic", "bad-key".encodeToByteArray())

        assertEquals(ProviderProbeVerdict.AUTH, verdict)
        assertTrue(verdict.definitive)
    }

    @Test
    fun `a provider with nothing to probe answers no model listed and frees the draft`() = runTest {
        val store = store()
        val coreApi = GatedProviderCoreApiClient()
        coreApi.probeVerdict = ProviderProbeVerdictView.NO_MODEL_LISTED
        val coordinator = coordinator(store, coreApi)
        val draft = "sk-not-a-real-key".encodeToByteArray()

        val verdict = coordinator.probeApiKey("openrouter", draft)

        assertEquals(ProviderProbeVerdict.NO_MODEL_LISTED, verdict)
        // Not a judgement of the key: the surface offers to save.
        assertFalse(verdict.definitive)
        // The ask was made and the handle crossed, but the core composed no
        // effect, so nothing spent the transient; the coordinator deletes it
        // rather than leaving it in the vault until profile close.
        assertEquals(1, coreApi.probed.size)
        assertEquals(0, store.clearTransient())
        // The caller's bytes are untouched, so a save can still follow.
        assertEquals("sk-not-a-real-key", draft.decodeToString())
    }

    @Test
    fun `a core that never answers is an unknown verdict rather than a wait forever`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        coreApi.probeVerdict = null
        val coordinator = coordinator(store(), coreApi)

        val verdict = coordinator.probeApiKey("anthropic", "key".encodeToByteArray())

        assertEquals(ProviderProbeVerdict.UNKNOWN, verdict)
        assertFalse(verdict.definitive)
    }

    @Test
    fun `a stale verdict from an earlier probe is not this probe's answer`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        // A verdict already on the books from some earlier probe.
        coreApi.publishProbeVerdict("anthropic", ProviderProbeVerdictView.AUTH)
        coreApi.probeVerdict = ProviderProbeVerdictView.USABLE
        val coordinator = coordinator(store(), coreApi)

        val verdict = coordinator.probeApiKey("anthropic", "new-key".encodeToByteArray())

        // The old row differs from the new one, so the new one is awaited and
        // the stale refusal is never read as this draft's judgement.
        assertEquals(ProviderProbeVerdict.USABLE, verdict)
    }

    private fun coordinator(
        store: AndroidProfileSecureMaterialStore,
        coreApi: GatedProviderCoreApiClient,
    ) = ProviderCredentialCoordinator(
        store,
        ProfileProviderCredentialsRepository(store, coreApi),
        coreApi,
        NoRevocationPort(),
    )

    private class NoRevocationPort : ProviderRevocationPort {
        override fun revokeBestEffort(providerId: String, token: ByteArray) = Unit
    }

    private fun store(secretBox: SecretBox = ReversingSecretBox()) =
        AndroidProfileSecureMaterialStore(
            MemoryPreferenceStore(),
            secretBox = secretBox,
            random = SecureRandom(),
        )

    private class GatedSecretBox : SecretBox {
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()

        override suspend fun seal(material: ByteArray): String {
            entered.complete(Unit)
            release.await()
            return encode(material)
        }

        override suspend fun open(sealed: String): ByteArray = decode(sealed)

        override fun close() = Unit
    }

    private class ReversingSecretBox : SecretBox {
        override suspend fun seal(material: ByteArray): String = encode(material)

        override suspend fun open(sealed: String): ByteArray = decode(sealed)

        override fun close() = Unit
    }

    private class MemoryPreferenceStore : ProfilePreferenceStore {
        private val strings = mutableMapOf<String, String>()

        override fun getString(name: String): String = strings[name].orEmpty()

        override fun putString(name: String, value: String) {
            strings[name] = value
        }

        override fun getBoolean(name: String): Boolean = false

        override fun putBoolean(name: String, value: Boolean) = Unit
    }

    private companion object {
        fun encode(material: ByteArray): String = Base64.getUrlEncoder()
            .withoutPadding()
            .encodeToString(material.reversedArray())

        fun decode(sealed: String): ByteArray =
            Base64.getUrlDecoder().decode(sealed).reversedArray()
    }
}
