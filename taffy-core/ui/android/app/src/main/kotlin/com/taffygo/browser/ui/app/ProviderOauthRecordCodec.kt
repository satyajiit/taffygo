// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.util.Base64

/**
 * The one encoding of a [ProviderOauthRecord] into store material.
 *
 * The store seals opaque bytes; this codec is what makes those bytes a
 * subscription record rather than a pasted key, and it is deliberately the
 * only place that knows the layout. The browser process never composes or
 * parses this format — it hands the parsed token response across the platform
 * seam as typed fields, and receives back typed access answers — so the
 * persisted shape can change here alone.
 *
 * The layout is seven fixed lines behind a marker:
 *
 * ```text
 * TAFFY-OAUTH-1
 * <token type>
 * <expiry, epoch milliseconds>
 * <scopes, space-joined, possibly blank>
 * <access token, unpadded base64url>
 * <refresh token, unpadded base64url, blank when absent>
 * <credential host, an https origin, blank when absent>
 * ```
 *
 * A six-line record decodes as one with no credential host rather than as a
 * corrupt one. The marker is unchanged on purpose: bumping it would make every
 * record written before this line read as a pasted key instead of a
 * subscription, which is a worse answer than the one this costs — six lines and
 * seven mean the same thing to a vendor that never named an address, and that
 * is every vendor but one.
 *
 * A record that fails any bound decodes to `null` rather than throwing: a
 * corrupt or overgrown record is a credential the resolver reports as
 * needing sign-in, never a crash on the model path. The marker line is what
 * tells the two record kinds apart, and [isOauthMaterial] is the only
 * question the key path asks — a pasted key beginning with the marker is
 * refused at save, so the answer can never be ambiguous later.
 */
object ProviderOauthRecordCodec {

    /** Whether sealed material is this codec's record rather than a raw key. */
    fun isOauthMaterial(material: ByteArray): Boolean =
        material.size > MARKER_LINE.length &&
            material.decodeToString(0, MARKER_LINE.length) == MARKER_LINE

    /** Encodes one record. The caller still owns (and zeroes) the record. */
    fun encode(record: ProviderOauthRecord): ByteArray {
        require(TOKEN_TYPE.matches(record.tokenType)) { "Malformed token type" }
        require(record.expiresAtEpochMs > 0) { "Expiry must be positive" }
        require(record.scopes.length <= MAX_SCOPES_CHARACTERS && SCOPES.matches(record.scopes)) {
            "Malformed scope list"
        }
        require(record.accessToken.isNotEmpty() && record.accessToken.size <= MAX_TOKEN_BYTES) {
            "Access token is empty or exceeds its bound"
        }
        require((record.refreshToken?.size ?: 1) in 1..MAX_TOKEN_BYTES) {
            "Refresh token is empty or exceeds its bound"
        }
        // A host with a separator in it would end its own line and start one
        // of its own choosing, which is the whole reason this layout can be
        // lines at all. The browser bounds the value where the vendor said it
        // and again where it addresses a request with it; this bound is about
        // the encoding rather than about the address.
        val host = record.credentialHost
        require(host == null || HOST_ORIGIN.matches(host)) { "Malformed credential host" }
        val access = BASE64.encodeToString(record.accessToken)
        val refresh = record.refreshToken?.let(BASE64::encodeToString).orEmpty()
        return buildString {
            append(MARKER_LINE).append('\n')
            append(record.tokenType).append('\n')
            append(record.expiresAtEpochMs).append('\n')
            append(record.scopes).append('\n')
            append(access).append('\n')
            append(refresh).append('\n')
            append(record.credentialHost.orEmpty())
        }.encodeToByteArray()
    }

    /** Decodes sealed material, or `null` for anything that is not a record. */
    fun decode(material: ByteArray): ProviderOauthRecord? {
        if (!isOauthMaterial(material)) return null
        val lines = material.decodeToString().split('\n')
        if (lines.size !in LINE_COUNT_WITHOUT_HOST..LINE_COUNT) return null
        if (lines[0] != MARKER_LINE) return null
        val tokenType = lines[1].takeIf(TOKEN_TYPE::matches) ?: return null
        val expiresAt = lines[2].toLongOrNull()?.takeIf { it > 0 } ?: return null
        val scopes = lines[3].takeIf {
            it.length <= MAX_SCOPES_CHARACTERS && SCOPES.matches(it)
        } ?: return null
        val access = decodeToken(lines[4]) ?: return null
        if (access.isEmpty()) return null
        val refresh: ByteArray? = if (lines[5].isEmpty()) {
            null
        } else {
            decodeToken(lines[5])?.takeIf { it.isNotEmpty() } ?: return null
        }
        val host: String? = lines.getOrNull(HOST_LINE)?.takeIf { it.isNotEmpty() }
        if (host != null && !HOST_ORIGIN.matches(host)) return null
        return ProviderOauthRecord(
            tokenType = tokenType,
            expiresAtEpochMs = expiresAt,
            scopes = scopes,
            accessToken = access,
            refreshToken = refresh,
            credentialHost = host,
        )
    }

    private fun decodeToken(encoded: String): ByteArray? {
        if (encoded.length > MAX_TOKEN_ENCODED_CHARACTERS) return null
        return try {
            Base64.getUrlDecoder().decode(encoded)
        } catch (_: IllegalArgumentException) {
            null
        }
    }

    /** The marker a pasted key must never begin with; the save path enforces it. */
    const val MARKER_LINE: String = "TAFFY-OAUTH-1"

    private const val LINE_COUNT = 7
    private const val LINE_COUNT_WITHOUT_HOST = 6
    private const val HOST_LINE = 6
    private const val MAX_TOKEN_BYTES = 4 * 1024
    private const val MAX_TOKEN_ENCODED_CHARACTERS = 6 * 1024
    private const val MAX_SCOPES_CHARACTERS = 1024
    private val BASE64 = Base64.getUrlEncoder().withoutPadding()
    private val TOKEN_TYPE = Regex("[A-Za-z][A-Za-z0-9._-]{0,31}")
    private val SCOPES = Regex("[\\x20-\\x7E]*")

    /**
     * An https origin and nothing else: no path, no port, no credentials, no
     * upper case. Narrower than a URL may be, because there is exactly one
     * spelling of an address in this product and the browser's own check —
     * `ProviderCredentialHostLicensed` — accepts exactly the same shape.
     */
    private const val LABEL = "[a-z0-9]([a-z0-9-]*[a-z0-9])?"
    private val HOST_ORIGIN = Regex("https://$LABEL(\\.$LABEL)+")
}
