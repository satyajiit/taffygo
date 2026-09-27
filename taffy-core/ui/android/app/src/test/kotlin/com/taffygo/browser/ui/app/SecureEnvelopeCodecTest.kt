// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.util.Base64
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class SecureEnvelopeCodecTest {
    @Test
    fun `valid bounded envelope round trips`() {
        val iv = ByteArray(SecureEnvelopeCodec.GCM_IV_BYTES) { it.toByte() }
        val ciphertext = ByteArray(SecureEnvelopeCodec.GCM_TAG_BYTES) { (it + 1).toByte() }

        val decoded = SecureEnvelopeCodec.decode(SecureEnvelopeCodec.encode(iv, ciphertext))

        assertArrayEquals(iv, decoded.iv)
        assertArrayEquals(ciphertext, decoded.ciphertext)
    }

    @Test
    fun `decoded iv and ciphertext are bounded before decryption`() {
        val oversizedIv = Base64.getUrlEncoder().withoutPadding()
            .encodeToString(ByteArray(SecureEnvelopeCodec.GCM_IV_BYTES + 1))
        val validCiphertext = Base64.getUrlEncoder().withoutPadding()
            .encodeToString(ByteArray(SecureEnvelopeCodec.GCM_TAG_BYTES))
        assertThrows(IllegalArgumentException::class.java) {
            SecureEnvelopeCodec.decode("1.$oversizedIv.$validCiphertext")
        }

        val validIv = Base64.getUrlEncoder().withoutPadding()
            .encodeToString(ByteArray(SecureEnvelopeCodec.GCM_IV_BYTES))
        val oversizedCiphertext = Base64.getUrlEncoder().withoutPadding()
            .encodeToString(ByteArray(SecureEnvelopeCodec.MAX_CIPHERTEXT_BYTES + 1))
        assertThrows(IllegalArgumentException::class.java) {
            SecureEnvelopeCodec.decode("1.$validIv.$oversizedCiphertext")
        }
    }

    @Test
    fun `malformed base64 is refused without partial decode`() {
        val validIv = Base64.getUrlEncoder().withoutPadding()
            .encodeToString(ByteArray(SecureEnvelopeCodec.GCM_IV_BYTES))
        assertThrows(IllegalArgumentException::class.java) {
            SecureEnvelopeCodec.decode("1.$validIv.${"A".repeat(21)}+")
        }
    }
}
