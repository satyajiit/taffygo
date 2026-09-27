// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

/** AES-GCM envelope whose non-exportable key lives in Android Keystore. */
internal class AndroidKeystoreSecretBox(
    private val alias: String,
    private val dispatcher: CoroutineDispatcher,
) : SecretBox {
    private val keyLock = Any()
    private var key: SecretKey? = null
    private var closed = false

    override suspend fun seal(material: ByteArray): String = withContext(dispatcher) {
        require(material.isNotEmpty() && material.size <= SecureEnvelopeCodec.MAX_MATERIAL_BYTES) {
            "Secure material is empty or exceeds its bound"
        }
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, key())
        val ciphertext = cipher.doFinal(material)
        SecureEnvelopeCodec.encode(cipher.iv, ciphertext)
    }

    override suspend fun open(sealed: String): ByteArray = withContext(dispatcher) {
        val decoded = SecureEnvelopeCodec.decode(sealed)
        try {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(
                Cipher.DECRYPT_MODE,
                key(),
                javax.crypto.spec.GCMParameterSpec(GCM_TAG_BITS, decoded.iv),
            )
            cipher.doFinal(decoded.ciphertext)
        } finally {
            decoded.iv.fill(0)
            decoded.ciphertext.fill(0)
        }
    }

    override fun close() {
        synchronized(keyLock) {
            if (closed) return
            closed = true
            key = null
        }
    }

    override suspend fun destroy(): Unit = withContext(dispatcher) {
        synchronized(keyLock) {
            closed = true
            key = null
        }
        val keyStore = KeyStore.getInstance(ANDROID_KEYSTORE)
        keyStore.load(null)
        if (keyStore.containsAlias(alias)) keyStore.deleteEntry(alias)
    }

    /** Loads lazily on the injected disk dispatcher; profile construction performs no key I/O. */
    private fun key(): SecretKey = synchronized(keyLock) {
        check(!closed) { "Profile Keystore is closed" }
        key?.let { return@synchronized it }
        loadOrCreateKey().also { key = it }
    }

    private fun loadOrCreateKey(): SecretKey {
        val keyStore = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        val existing = keyStore.getKey(alias, null)
        if (existing != null) {
            require(existing is SecretKey) { "Profile Keystore entry has the wrong type" }
            return existing
        }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE).run {
            init(
                KeyGenParameterSpec.Builder(
                    alias,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                )
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(AES_KEY_BITS)
                    .setRandomizedEncryptionRequired(true)
                    .build(),
            )
            generateKey()
        }
    }

    private companion object {
        const val ANDROID_KEYSTORE = "AndroidKeyStore"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val AES_KEY_BITS = 256
        const val GCM_TAG_BITS = 128
    }
}
