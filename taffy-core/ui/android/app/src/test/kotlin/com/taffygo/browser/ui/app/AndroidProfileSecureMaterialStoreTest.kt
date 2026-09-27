// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.security.SecureRandom
import java.util.Base64
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class AndroidProfileSecureMaterialStoreTest {
    @Test
    fun `transient material is zeroed encrypted and spent exactly once`() = runTest {
        val preferences = MemoryPreferenceStore()
        val box = ReversingSecretBox()
        val store = AndroidProfileSecureMaterialStore(
            preferences,
            secretBox = box,
            random = SecureRandom(),
        )
        val material = "temporary-code".encodeToByteArray()
        val expected = material.copyOf()

        val handle = store.writeTransient(material)

        assertTrue(material.all { it == 0.toByte() })
        assertFalse(box.lastEnvelope.orEmpty().contains("temporary-code"))
        assertArrayEquals(expected, store.consume(handle))
        assertTrue(runCatching { store.consume(handle) }.exceptionOrNull() is IllegalStateException)
    }

    @Test
    fun `transient capacity is bounded and consume delete and clear release it`() = runTest {
        val store = store(MemoryPreferenceStore())
        val handles = (0 until 16).map { store.writeTransient(byteArrayOf(it.toByte())) }
        val refused = byteArrayOf(99)
        assertTrue(
            runCatching { store.writeTransient(refused) }.exceptionOrNull()
                is IllegalStateException,
        )
        assertTrue(refused.all { it == 0.toByte() })

        store.consume(handles.first()).fill(0)
        val replacement = store.writeTransient(byteArrayOf(100))
        assertTrue(store.delete(replacement))
        assertEquals(15, store.clearTransient())
        assertEquals(0, store.clearTransient())
    }

    @Test
    fun `provider storage persists ciphertext and projects only provider ids`() = runTest {
        val preferences = MemoryPreferenceStore()
        val store = AndroidProfileSecureMaterialStore(
            preferences,
            secretBox = ReversingSecretBox(),
            random = SecureRandom(),
        )
        val material = "provider-secret".encodeToByteArray()

        store.storeProviderCredential("openai", material)

        assertTrue(material.all { it == 0.toByte() })
        assertTrue(store.configuredProviderIds() == setOf("openai"))
        assertFalse(
            preferences.getString(ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS)
                .contains("provider-secret"),
        )
        assertArrayEquals(
            "provider-secret".encodeToByteArray(),
            store.resolveProviderCredential("openai"),
        )
    }

    @Test
    fun `regular session survives store recreation and rotates one complete record`() = runTest {
        val preferences = MemoryPreferenceStore()
        val firstStore = store(preferences)
        val initialMaterial = "access-one\nrefresh-one".encodeToByteArray()

        val first = firstStore.rotateSession(null, 0uL, initialMaterial)

        assertTrue(initialMaterial.all { it == 0.toByte() })
        assertEquals(0uL, first.rotation)
        assertFalse(
            preferences.getString(ProfilePreferenceNames.ACCOUNT_SESSION_RECORDS)
                .contains("refresh-one"),
        )
        val restoredStore = store(preferences)
        assertArrayEquals(
            "access-one\nrefresh-one".encodeToByteArray(),
            restoredStore.readSession(first.handle, first.rotation),
        )

        val refreshedMaterial = "access-two\nrefresh-two".encodeToByteArray()
        val refreshed = restoredStore.rotateSession(
            first.handle,
            first.rotation + 1uL,
            refreshedMaterial,
        )

        assertEquals(first.handle, refreshed.handle)
        assertEquals(1uL, refreshed.rotation)
        assertTrue(refreshedMaterial.all { it == 0.toByte() })
        assertTrue(
            runCatching { restoredStore.readSession(first.handle, first.rotation) }
                .exceptionOrNull() is IllegalStateException,
        )
        assertThrows(IllegalStateException::class.java) {
            restoredStore.deleteSession(first.handle, first.rotation)
        }
        assertArrayEquals(
            "access-two\nrefresh-two".encodeToByteArray(),
            restoredStore.readSession(refreshed.handle, refreshed.rotation),
        )
    }

    @Test
    fun `session stale rotation and duplicate delete fail closed`() = runTest {
        val store = store(MemoryPreferenceStore())
        val session = store.rotateSession(null, 0uL, byteArrayOf(1, 2, 3))

        assertTrue(
            runCatching { store.readSession(session.handle, session.rotation + 1uL) }
                .exceptionOrNull() is IllegalStateException,
        )
        assertThrows(IllegalStateException::class.java) {
            store.deleteSession(session.handle, session.rotation + 1uL)
        }
        assertTrue(store.deleteSession(session.handle, session.rotation))
        assertFalse(store.deleteSession(session.handle, session.rotation))
    }

    @Test
    fun `second initial session cannot orphan the canonical record`() = runTest {
        val store = store(MemoryPreferenceStore())
        val first = store.rotateSession(null, 0uL, byteArrayOf(1, 2, 3))
        val duplicate = byteArrayOf(4, 5, 6)

        assertTrue(
            runCatching { store.rotateSession(null, 0uL, duplicate) }
                .exceptionOrNull() is IllegalStateException,
        )
        assertTrue(duplicate.all { it == 0.toByte() })
        assertArrayEquals(byteArrayOf(1, 2, 3), store.readSession(first.handle, 0uL))
    }

    @Test
    fun `fail closed reconciliation wipes the canonical session`() = runTest {
        val store = store(MemoryPreferenceStore())
        val session = store.rotateSession(null, 0uL, byteArrayOf(1, 2, 3))

        assertTrue(store.clearSession())
        assertFalse(store.clearSession())
        assertTrue(
            runCatching { store.readSession(session.handle, session.rotation) }
                .exceptionOrNull() is IllegalStateException,
        )
    }

    @Test
    fun `fail closed reconciliation can wipe a malformed session registry`() {
        val preferences = MemoryPreferenceStore().apply {
            putString(ProfilePreferenceNames.ACCOUNT_SESSION_RECORDS, "malformed")
        }
        val store = store(preferences)

        assertThrows(IllegalArgumentException::class.java) {
            store.inspectCurrentSession()
        }
        assertTrue(store.clearSession())
        assertEquals(null, store.inspectCurrentSession())
    }

    @Test
    fun `bootstrap inspection reads only canonical metadata`() = runTest {
        val box = ReversingSecretBox()
        val store = AndroidProfileSecureMaterialStore(
            MemoryPreferenceStore(),
            secretBox = box,
            random = SecureRandom(),
        )
        assertEquals(null, store.inspectCurrentSession())
        val session = store.rotateSession(null, 0uL, byteArrayOf(1, 2, 3))

        assertEquals(session, store.inspectCurrentSession())
        assertEquals(0, box.openCount)
    }

    @Test
    fun `platform entropy is generated at the requested bounded size`() = runTest {
        val store = store(MemoryPreferenceStore())

        val first = store.generateEntropy(32)
        assertTrue(first.size == 32)
        assertTrue(
            runCatching { store.generateEntropy(65) }.exceptionOrNull()
                is IllegalArgumentException,
        )
    }

    @Test
    fun `corrupt provider registry is refused without a partial projection`() {
        val preferences = MemoryPreferenceStore().apply {
            putString(
                ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS,
                "openai\tvalid-envelope\nmissing-separator",
            )
        }
        val store = store(preferences)

        assertThrows(IllegalArgumentException::class.java) {
            store.configuredProviderIds()
        }
    }

    @Test
    fun `oversized registry and ciphertext are refused before decode`() {
        val preferences = MemoryPreferenceStore().apply {
            putString(
                ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS,
                "openai\t${"A".repeat(500_000)}",
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            store(preferences).configuredProviderIds()
        }

        // One character past the 16384-char per-record bound, which was
        // raised from 8192 when the subscription triple became one record.
        preferences.putString(
            ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS,
            "openai\t${"A".repeat(16_385)}",
        )
        assertThrows(IllegalArgumentException::class.java) {
            store(preferences).configuredProviderIds()
        }
    }

    @Test
    fun `duplicate provider record is refused instead of using the last value`() {
        val preferences = MemoryPreferenceStore().apply {
            putString(
                ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS,
                "openai\tfirst\nopenai\tsecond",
            )
        }

        assertThrows(IllegalArgumentException::class.java) {
            store(preferences).configuredProviderIds()
        }
    }

    @Test
    fun `provider record count is bounded independently of encoded size`() {
        val preferences = MemoryPreferenceStore().apply {
            putString(
                ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS,
                (0..32).joinToString("\n") { index -> "provider-$index\tvalue" },
            )
        }

        assertThrows(IllegalArgumentException::class.java) {
            store(preferences).configuredProviderIds()
        }
    }

    @Test
    fun `profile erasure clears every record and destroys the profile key`() = runTest {
        val preferences = MemoryPreferenceStore()
        val box = ReversingSecretBox()
        val store = AndroidProfileSecureMaterialStore(
            preferences,
            secretBox = box,
            random = SecureRandom(),
        )
        store.rotateSession(null, 0uL, byteArrayOf(1, 2, 3))
        store.storeProviderCredential("openai", byteArrayOf(4, 5, 6))
        store.writeTransient(byteArrayOf(7, 8, 9))

        store.destroyAll()

        assertEquals("", preferences.getString(ProfilePreferenceNames.ACCOUNT_SESSION_RECORDS))
        assertEquals("", preferences.getString(ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS))
        assertTrue(box.destroyed)
        assertEquals(0, store.clearTransient())
    }

    private fun store(preferences: ProfilePreferenceStore) =
        AndroidProfileSecureMaterialStore(
            preferences,
            secretBox = ReversingSecretBox(),
            random = SecureRandom(),
        )

    private class ReversingSecretBox : SecretBox {
        var lastEnvelope: String? = null
        var openCount: Int = 0
        var destroyed: Boolean = false

        override suspend fun seal(material: ByteArray): String = Base64.getUrlEncoder()
            .withoutPadding()
            .encodeToString(material.reversedArray())
            .also { lastEnvelope = it }

        override suspend fun open(sealed: String): ByteArray {
            openCount += 1
            return Base64.getUrlDecoder().decode(sealed).reversedArray()
        }

        override suspend fun destroy() {
            destroyed = true
        }

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
