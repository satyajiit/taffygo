// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import java.security.SecureRandom
import java.util.Base64
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.ProviderAuthMethodView

/**
 * A stored provider key and a core that knows about it are one act.
 *
 * The failure these tests exist for makes no noise: the key is genuinely sealed
 * on the disk, the core's credential directory is empty, and every direct route
 * is refused for want of a credential that is right there. Both sides behave
 * exactly as written, so only an assertion that the two happened together
 * catches it.
 */
class ProfileProviderCredentialsRepositoryTest {

    @Test
    fun `saving a key seals it and hands the core the record's name`() = runTest {
        val coreApi = RecordingProviderCoreApiClient()
        val store = store()
        val repository = ProfileProviderCredentialsRepository(store, coreApi)

        repository.saveApiKey("openai", "sk-not-a-real-key".encodeToByteArray())

        assertEquals(
            listOf(
                RecordingProviderCoreApiClient.SavedCredential(
                    providerId = "openai",
                    authMethod = ProviderAuthMethodView.API_KEY,
                    credentialHandle = "openai",
                ),
            ),
            coreApi.saved,
        )
        assertEquals(setOf("openai"), repository.configuredProviderIds.value)
        assertArrayEquals(
            "sk-not-a-real-key".encodeToByteArray(),
            store.resolveProviderCredential("openai"),
        )
    }

    /** No parameter of the command is the key, and this asserts it rather than says it. */
    @Test
    fun `nothing the core is told is the material`() = runTest {
        val coreApi = RecordingProviderCoreApiClient()
        val repository = ProfileProviderCredentialsRepository(store(), coreApi)

        repository.saveApiKey("anthropic", "sk-ant-not-a-real-key".encodeToByteArray())

        val told = coreApi.saved.single()
        assertFalse(told.providerId.contains("sk-ant"))
        assertFalse(told.credentialHandle.contains("sk-ant"))
    }

    /**
     * A refused command takes the record with it.
     *
     * Leaving it would draw a configured row over a provider the core will
     * refuse, which is the disagreement this class exists to close, reached by
     * the one path that could still produce it.
     */
    @Test
    fun `a refused save leaves no credential behind`() = runTest {
        val coreApi = RecordingProviderCoreApiClient(refuse = true)
        val store = store()
        val repository = ProfileProviderCredentialsRepository(store, coreApi)

        val refusal = runCatching {
            repository.saveApiKey("openai", "sk-not-a-real-key".encodeToByteArray())
        }.exceptionOrNull()

        assertTrue(refusal is CoreApiSubmissionException)
        assertEquals(
            CoreApiSubmissionException.Reason.CORE_UNAVAILABLE,
            (refusal as CoreApiSubmissionException).reason,
        )
        assertTrue(repository.configuredProviderIds.value.isEmpty())
        assertTrue(store.configuredProviderIds().isEmpty())
    }

    /**
     * The half that seals and says nothing, and why the silence is right.
     *
     * A provider a person defines is written by one command carrying its
     * address, its name, its models and its credential together (decision 0096
     * section 4). Announcing the credential here as well would be the second
     * command that record refuses, and it would name a provider the core has
     * not been given yet.
     */
    @Test
    fun `sealing a key answers the record's name and tells the core nothing`() = runTest {
        val coreApi = RecordingProviderCoreApiClient()
        val store = store()
        val repository = ProfileProviderCredentialsRepository(store, coreApi)

        val handle = repository.sealApiKey("my-laptop", "sk-local-not-a-real-key".encodeToByteArray())

        assertEquals("my-laptop", handle)
        assertTrue(coreApi.saved.isEmpty())
        assertEquals(setOf("my-laptop"), repository.configuredProviderIds.value)
        assertArrayEquals(
            "sk-local-not-a-real-key".encodeToByteArray(),
            store.resolveProviderCredential("my-laptop"),
        )
    }

    /**
     * The read a one-write save needs: what is already there, so an edit can
     * carry the credential it is not changing rather than silently dropping it.
     */
    @Test
    fun `the handle held for a provider is answered, and nothing is invented`() = runTest {
        val repository = ProfileProviderCredentialsRepository(
            store(),
            RecordingProviderCoreApiClient(),
        )
        repository.sealApiKey("my-laptop", "sk-local-not-a-real-key".encodeToByteArray())

        assertEquals("my-laptop", repository.heldCredentialHandle("my-laptop"))
        assertNull(repository.heldCredentialHandle("the-other-box"))
    }

    @Test
    fun `forgetting revokes the record and the core's reference to it`() = runTest {
        val coreApi = RecordingProviderCoreApiClient()
        val store = store()
        val repository = ProfileProviderCredentialsRepository(store, coreApi)
        repository.saveApiKey("openai", "sk-not-a-real-key".encodeToByteArray())

        repository.forget("openai")

        assertEquals(listOf("openai"), coreApi.forgotten)
        assertTrue(repository.configuredProviderIds.value.isEmpty())
        assertTrue(store.configuredProviderIds().isEmpty())
    }

    /**
     * A key stored before the core was ever told about one still comes off the
     * device.
     *
     * The core refuses a revoke for a provider it holds no credential for, and
     * that is every key on a device that predates this class. Removing first is
     * what makes the button work at all; the refusal is still raised.
     */
    @Test
    fun `a refused forget still takes the key off the device`() = runTest {
        val store = store()
        ProfileProviderCredentialsRepository(store, RecordingProviderCoreApiClient())
            .saveApiKey("openai", "sk-not-a-real-key".encodeToByteArray())
        val repository = ProfileProviderCredentialsRepository(
            store,
            RecordingProviderCoreApiClient(refuse = true),
        )

        val refusal = runCatching { repository.forget("openai") }.exceptionOrNull()

        assertTrue(refusal is CoreApiSubmissionException)
        assertTrue(store.configuredProviderIds().isEmpty())
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
