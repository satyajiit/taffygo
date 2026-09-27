// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.util.Base64

/** Bounded, allocation-safe codec for profile Keystore envelopes. */
internal object SecureEnvelopeCodec {
    data class Decoded(
        val iv: ByteArray,
        val ciphertext: ByteArray,
    )

    fun encode(iv: ByteArray, ciphertext: ByteArray): String {
        require(iv.size == GCM_IV_BYTES) { "Malformed secure-material IV" }
        require(ciphertext.size in GCM_TAG_BYTES..MAX_CIPHERTEXT_BYTES) {
            "Secure-material ciphertext exceeds its bound"
        }
        return "$ENVELOPE_VERSION.${iv.toBase64Url()}.${ciphertext.toBase64Url()}"
    }

    fun decode(sealed: String): Decoded {
        require(sealed.length in MIN_ENVELOPE_CHARACTERS..MAX_ENVELOPE_CHARACTERS) {
            "Secure-material envelope exceeds its bound"
        }
        val firstSeparator = sealed.indexOf(ENVELOPE_SEPARATOR)
        val secondSeparator = sealed.indexOf(ENVELOPE_SEPARATOR, firstSeparator + 1)
        require(
            firstSeparator == ENVELOPE_VERSION.length &&
                secondSeparator > firstSeparator + 1 &&
                sealed.indexOf(ENVELOPE_SEPARATOR, secondSeparator + 1) == -1 &&
                sealed.startsWith(ENVELOPE_VERSION),
        ) {
            "Unsupported secure-material envelope"
        }
        val encodedIv = sealed.substring(firstSeparator + 1, secondSeparator)
        val encodedCiphertext = sealed.substring(secondSeparator + 1)
        require(encodedIv.length == GCM_IV_BASE64_CHARACTERS) {
            "Malformed secure-material IV"
        }
        require(
            encodedCiphertext.length in GCM_TAG_BASE64_CHARACTERS..MAX_CIPHERTEXT_BASE64_CHARACTERS,
        ) {
            "Secure-material ciphertext exceeds its bound"
        }
        require(encodedIv.isBase64Url() && encodedCiphertext.isBase64Url()) {
            "Malformed secure-material envelope encoding"
        }
        val iv = Base64.getUrlDecoder().decode(encodedIv)
        val ciphertext = Base64.getUrlDecoder().decode(encodedCiphertext)
        require(iv.size == GCM_IV_BYTES) { "Malformed secure-material IV" }
        require(ciphertext.size in GCM_TAG_BYTES..MAX_CIPHERTEXT_BYTES) {
            "Secure-material ciphertext exceeds its bound"
        }
        return Decoded(iv, ciphertext)
    }

    private fun ByteArray.toBase64Url(): String =
        Base64.getUrlEncoder().withoutPadding().encodeToString(this)

    private fun String.isBase64Url(): Boolean = all { character ->
        character in 'A'..'Z' ||
            character in 'a'..'z' ||
            character in '0'..'9' ||
            character == '-' ||
            character == '_'
    }

    const val GCM_IV_BYTES = 12
    const val GCM_TAG_BYTES = 16
    const val MAX_MATERIAL_BYTES = 64 * 1024
    const val MAX_CIPHERTEXT_BYTES = MAX_MATERIAL_BYTES + GCM_TAG_BYTES

    private const val ENVELOPE_VERSION = "1"
    private const val ENVELOPE_SEPARATOR = '.'
    private const val GCM_IV_BASE64_CHARACTERS = 16
    private const val GCM_TAG_BASE64_CHARACTERS = 22
    private const val MAX_CIPHERTEXT_BASE64_CHARACTERS =
        (MAX_CIPHERTEXT_BYTES * 4 + 2) / 3
    private const val MIN_ENVELOPE_CHARACTERS =
        ENVELOPE_VERSION.length + 2 + GCM_IV_BASE64_CHARACTERS + GCM_TAG_BASE64_CHARACTERS
    private const val MAX_ENVELOPE_CHARACTERS =
        ENVELOPE_VERSION.length + 2 + GCM_IV_BASE64_CHARACTERS +
            MAX_CIPHERTEXT_BASE64_CHARACTERS
}
