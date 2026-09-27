// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.security.SecureRandom
import java.util.Base64
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView

/**
 * The interleavings decision 0078 exists to close.
 *
 * Each race is driven through explicit gates rather than timing: the first
 * operation is released only after the second is provably parked on the
 * provider's critical section, so every test proves an ordering, not a
 * coincidence. The store fake is the real store class over an in-memory
 * preference store, which is what makes the in-section re-read a read of the
 * real thing.
 */
class ProviderCredentialCoordinatorTest {

    @Test
    fun `a state report that raced a forget observes the deletion and drops itself`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val store = store()
        val coordinator = coordinator(store, coreApi)
        coordinator.saveApiKey("openai", "sk-not-a-real-key".encodeToByteArray())
        coreApi.gateNext()

        // The forget enters the section and parks inside the core command,
        // holding the provider's lock with the store already cleared.
        val forget = launch { coordinator.forget("openai") }
        coreApi.entered.await()

        // The report arrives while the forget holds the section.
        var reported: Boolean? = null
        val report = launch {
            reported = coordinator.reportCredentialState(
                "openai",
                ProviderCredentialStateView.REFRESH_FAILED,
            )
        }
        testScheduler.advanceUntilIdle()
        assertEquals(null, reported)

        coreApi.release.complete(Unit)
        forget.join()
        report.join()

        assertEquals(false, reported)
        assertEquals(
            "the dropped report must never reach the core",
            emptyList<GatedProviderCoreApiClient.ReportedState>(),
            coreApi.reportedStates,
        )
        assertEquals(listOf("openai"), coreApi.forgotten)
    }

    @Test
    fun `a report for a record that is still stored reaches the core`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val coordinator = coordinator(store(), coreApi)
        coordinator.saveApiKey("xai", "xai-not-a-real-key".encodeToByteArray())

        val sent = coordinator.reportCredentialState(
            "xai",
            ProviderCredentialStateView.NEEDS_SIGN_IN,
        )

        assertTrue(sent)
        assertEquals(
            listOf(
                GatedProviderCoreApiClient.ReportedState(
                    "xai",
                    ProviderCredentialStateView.NEEDS_SIGN_IN,
                ),
            ),
            coreApi.reportedStates,
        )
    }

    @Test
    fun `two saves against one provider serialize instead of interleaving`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val store = store()
        val coordinator = coordinator(store, coreApi)
        coreApi.gateNext()

        val first = launch {
            coordinator.saveApiKey("openai", "first-not-a-real-key".encodeToByteArray())
        }
        coreApi.entered.await()

        val second = launch {
            coordinator.saveApiKey("openai", "second-not-a-real-key".encodeToByteArray())
        }
        testScheduler.advanceUntilIdle()
        // While the first save is parked inside its core command, the second
        // has not touched the store: one writer at a time is the whole point.
        assertEquals(1, coreApi.saved.size)

        coreApi.release.complete(Unit)
        first.join()
        second.join()

        assertEquals(listOf("openai", "openai"), coreApi.saved.map { it.providerId })
        assertEquals(
            "the last serialized writer's material is what the store holds",
            "second-not-a-real-key",
            store.resolveProviderCredential("openai").decodeToString(),
        )
    }

    @Test
    fun `sections for different providers are independent`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val coordinator = coordinator(store(), coreApi)
        coreApi.gateNext()

        // One provider's save parks inside its core command and holds only its
        // own section.
        val parked = launch {
            coordinator.saveApiKey("openai", "sk-not-a-real-key".encodeToByteArray())
        }
        coreApi.entered.await()

        // Another provider's save runs to completion straight through.
        coordinator.saveApiKey("xai", "xai-not-a-real-key".encodeToByteArray())
        assertEquals(listOf("openai", "xai"), coreApi.saved.map { it.providerId })

        coreApi.release.complete(Unit)
        parked.join()
    }

    @Test
    fun `a report about a record that was never stored is dropped without a section side effect`() =
        runTest {
            val coreApi = GatedProviderCoreApiClient()
            val coordinator = coordinator(store(), coreApi)

            val sent = coordinator.reportCredentialState(
                "openai",
                ProviderCredentialStateView.USABLE,
            )

            assertFalse(sent)
            assertTrue(coreApi.reportedStates.isEmpty())
        }

    @Test
    fun `a pasted key resolves as itself`() = runTest {
        val coordinator = coordinator(store(), GatedProviderCoreApiClient())
        coordinator.saveApiKey("openai", "sk-not-a-real-key".encodeToByteArray())

        val access = coordinator.resolveAccess("openai", nowEpochMs = 1_000)

        assertTrue(access is ProviderAccess.RawKey)
        assertEquals(
            "sk-not-a-real-key",
            (access as ProviderAccess.RawKey).material.decodeToString(),
        )
    }

    @Test
    fun `a provider with no record resolves as not configured`() = runTest {
        val coordinator = coordinator(store(), GatedProviderCoreApiClient())

        assertEquals(
            ProviderAccess.NotConfigured,
            coordinator.resolveAccess("openai", nowEpochMs = 1_000),
        )
    }

    @Test
    fun `a completed sign-in is sealed and announced as one OAUTH record`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val coordinator = coordinator(store(), coreApi)

        coordinator.completeSignIn("xai", record(expiresAtEpochMs = 10_000_000))

        assertEquals(
            listOf(
                GatedProviderCoreApiClient.SavedCredential(
                    "xai",
                    ProviderAuthMethodView.OAUTH,
                    "xai",
                ),
            ),
            coreApi.saved,
        )
        val access = coordinator.resolveAccess("xai", nowEpochMs = 1_000)
        assertTrue(access is ProviderAccess.AccessToken)
        assertEquals(
            "access-not-a-real-token",
            (access as ProviderAccess.AccessToken).material.decodeToString(),
        )
    }

    @Test
    fun `a token inside the resolve margin demands the one refresh leg`() = runTest {
        val coordinator = coordinator(store(), GatedProviderCoreApiClient())
        coordinator.completeSignIn("xai", record(expiresAtEpochMs = 100_000))

        // 60 seconds before expiry is decision 0078's margin: at 50 seconds
        // remaining the token still works and is already not fresh enough.
        val access = coordinator.resolveAccess("xai", nowEpochMs = 50_000)

        assertTrue(access is ProviderAccess.RefreshRequired)
        assertEquals(
            "refresh-not-a-real-token",
            (access as ProviderAccess.RefreshRequired).refreshToken.decodeToString(),
        )
    }

    @Test
    fun `a stale token with no refresh credential needs the person`() = runTest {
        val coordinator = coordinator(store(), GatedProviderCoreApiClient())
        coordinator.completeSignIn(
            "xai",
            ProviderOauthRecord(
                tokenType = "Bearer",
                expiresAtEpochMs = 100_000,
                scopes = "",
                accessToken = "access-not-a-real-token".encodeToByteArray(),
                refreshToken = null,
            ),
        )

        assertEquals(
            ProviderAccess.SignInRequired,
            coordinator.resolveAccess("xai", nowEpochMs = 90_000),
        )
    }

    @Test
    fun `a kept rotation refiles usable and answers the new token`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val coordinator = coordinator(store(), coreApi)
        coordinator.completeSignIn("xai", record(expiresAtEpochMs = 100_000))

        val kept = coordinator.storeRefreshedRecord(
            "xai",
            ProviderOauthRecord(
                tokenType = "Bearer",
                expiresAtEpochMs = 10_000_000,
                scopes = "",
                accessToken = "rotated-not-a-real-token".encodeToByteArray(),
                refreshToken = "refresh-2-not-a-real-token".encodeToByteArray(),
            ),
        )

        assertTrue(kept)
        assertEquals(
            listOf(
                GatedProviderCoreApiClient.ReportedState(
                    "xai",
                    ProviderCredentialStateView.USABLE,
                ),
            ),
            coreApi.reportedStates,
        )
        val access = coordinator.resolveAccess("xai", nowEpochMs = 1_000)
        assertEquals(
            "rotated-not-a-real-token",
            (access as ProviderAccess.AccessToken).material.decodeToString(),
        )
    }

    @Test
    fun `a rotation that raced a forget is dropped whole`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val coordinator = coordinator(store(), coreApi)
        coordinator.completeSignIn("xai", record(expiresAtEpochMs = 100_000))
        coordinator.forget("xai")

        val kept = coordinator.storeRefreshedRecord(
            "xai",
            record(expiresAtEpochMs = 10_000_000),
        )

        assertFalse(kept)
        assertEquals(
            "a dropped rotation reports nothing",
            emptyList<GatedProviderCoreApiClient.ReportedState>(),
            coreApi.reportedStates,
        )
        assertEquals(
            ProviderAccess.NotConfigured,
            coordinator.resolveAccess("xai", nowEpochMs = 1_000),
        )
    }

    @Test
    fun `a pasted key wearing the record marker is refused at the door`() = runTest {
        val coordinator = coordinator(store(), GatedProviderCoreApiClient())

        val refused = runCatching {
            coordinator.saveApiKey("openai", "TAFFY-OAUTH-1\nnot-a-key".encodeToByteArray())
        }

        assertTrue(refused.exceptionOrNull() is IllegalArgumentException)
        assertEquals(
            ProviderAccess.NotConfigured,
            coordinator.resolveAccess("openai", nowEpochMs = 1_000),
        )
    }

    @Test
    fun `a subscription sign-out revokes the refresh token before the record dies`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val store = store()
        val revocation = RecordingRevocationPort()
        val coordinator = coordinator(store, coreApi, revocation)
        coordinator.completeSignIn("xai", record(expiresAtEpochMs = 10_000))

        coordinator.forget("xai")

        assertEquals(
            listOf(RecordingRevocationPort.Revoked("xai", "refresh-not-a-real-token")),
            revocation.revoked,
        )
        assertEquals(listOf("xai"), coreApi.forgotten)
        assertFalse("xai" in store.configuredProviderIds())
    }

    @Test
    fun `a record with no refresh token offers its access token to the revocation`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val store = store()
        val revocation = RecordingRevocationPort()
        val coordinator = coordinator(store, coreApi, revocation)
        coordinator.completeSignIn(
            "xai",
            ProviderOauthRecord(
                tokenType = "Bearer",
                expiresAtEpochMs = 10_000,
                scopes = "",
                accessToken = "access-not-a-real-token".encodeToByteArray(),
                refreshToken = null,
            ),
        )

        coordinator.forget("xai")

        assertEquals(
            listOf(RecordingRevocationPort.Revoked("xai", "access-not-a-real-token")),
            revocation.revoked,
        )
        assertEquals(listOf("xai"), coreApi.forgotten)
    }

    @Test
    fun `a pasted key is forgotten without touching the revocation seam`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val revocation = RecordingRevocationPort()
        val coordinator = coordinator(store(), coreApi, revocation)
        coordinator.saveApiKey("openai", "sk-not-a-real-key".encodeToByteArray())

        coordinator.forget("openai")

        assertEquals(
            "a raw key has no grant at any vendor to revoke",
            emptyList<RecordingRevocationPort.Revoked>(),
            revocation.revoked,
        )
        assertEquals(listOf("openai"), coreApi.forgotten)
    }

    @Test
    fun `a revocation port that throws does not stop the forget`() = runTest {
        val coreApi = GatedProviderCoreApiClient()
        val store = store()
        val coordinator = coordinator(
            store,
            coreApi,
            revocation = { _, _ -> throw IllegalStateException("vendor unreachable") },
        )
        coordinator.completeSignIn("xai", record(expiresAtEpochMs = 10_000))

        coordinator.forget("xai")

        assertEquals(listOf("xai"), coreApi.forgotten)
        assertFalse("xai" in store.configuredProviderIds())
    }

    private fun record(expiresAtEpochMs: Long) = ProviderOauthRecord(
        tokenType = "Bearer",
        expiresAtEpochMs = expiresAtEpochMs,
        scopes = "offline_access",
        accessToken = "access-not-a-real-token".encodeToByteArray(),
        refreshToken = "refresh-not-a-real-token".encodeToByteArray(),
    )

    private fun coordinator(
        store: AndroidProfileSecureMaterialStore,
        coreApi: GatedProviderCoreApiClient,
        revocation: ProviderRevocationPort = RecordingRevocationPort(),
    ) = ProviderCredentialCoordinator(
        store,
        ProfileProviderCredentialsRepository(store, coreApi),
        coreApi,
        revocation,
    )

    private class RecordingRevocationPort : ProviderRevocationPort {
        data class Revoked(val providerId: String, val token: String)

        val revoked = mutableListOf<Revoked>()

        override fun revokeBestEffort(providerId: String, token: ByteArray) {
            revoked += Revoked(providerId, token.decodeToString())
        }
    }

    private fun store() = AndroidProfileSecureMaterialStore(
        MemoryPreferenceStore(),
        secretBox = ReversingSecretBox(),
        random = SecureRandom(),
    )

    private class ReversingSecretBox : SecretBox {
        override suspend fun seal(material: ByteArray): String = Base64.getUrlEncoder()
            .withoutPadding()
            .encodeToString(material.reversedArray())

        override suspend fun open(sealed: String): ByteArray =
            Base64.getUrlDecoder().decode(sealed).reversedArray()

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
}
