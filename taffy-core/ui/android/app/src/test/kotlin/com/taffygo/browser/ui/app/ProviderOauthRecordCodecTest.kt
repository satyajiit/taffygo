// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The one persisted subscription-record encoding. */
internal class ProviderOauthRecordCodecTest {

    @Test
    fun `a full triple survives the round trip`() {
        val record = ProviderOauthRecord(
            tokenType = "Bearer",
            expiresAtEpochMs = 1_800_000_000_000,
            scopes = "offline_access chat",
            accessToken = "access-not-a-real-token".toByteArray(),
            refreshToken = "refresh-not-a-real-token".toByteArray(),
        )

        val decoded = ProviderOauthRecordCodec.decode(ProviderOauthRecordCodec.encode(record))

        requireNotNull(decoded)
        assertEquals("Bearer", decoded.tokenType)
        assertEquals(1_800_000_000_000, decoded.expiresAtEpochMs)
        assertEquals("offline_access chat", decoded.scopes)
        assertArrayEquals(record.accessToken, decoded.accessToken)
        assertArrayEquals(record.refreshToken, decoded.refreshToken)
    }

    @Test
    fun `an issued address survives the round trip`() {
        val record = ProviderOauthRecord(
            tokenType = "Bearer",
            expiresAtEpochMs = 1,
            scopes = "",
            accessToken = byteArrayOf(1),
            refreshToken = null,
            credentialHost = "https://one.api.vendor.example",
        )

        val decoded = ProviderOauthRecordCodec.decode(ProviderOauthRecordCodec.encode(record))

        requireNotNull(decoded)
        assertEquals("https://one.api.vendor.example", decoded.credentialHost)
    }

    // The six-line shape predates the address line. It is a record from a
    // vendor that names no address, which is every vendor but one, so it
    // decodes rather than reading as damage.
    @Test
    fun `a record written before the address line still decodes`() {
        val legacy = "TAFFY-OAUTH-1\nBearer\n5\n\nCQ\n"

        val decoded = ProviderOauthRecordCodec.decode(legacy.encodeToByteArray())

        requireNotNull(decoded)
        assertNull(decoded.credentialHost)
    }

    @Test
    fun `an address that is not a bare https origin is refused`() {
        for (malformed in listOf(
            "http://one.api.vendor.example",
            "https://one.api.vendor.example/v1",
            "https://ONE.api.vendor.example",
            "one.api.vendor.example",
        )) {
            val record = ProviderOauthRecord(
                tokenType = "Bearer",
                expiresAtEpochMs = 1,
                scopes = "",
                accessToken = byteArrayOf(1),
                refreshToken = null,
                credentialHost = malformed,
            )
            assertTrue(
                malformed,
                runCatching { ProviderOauthRecordCodec.encode(record) }
                    .exceptionOrNull() is IllegalArgumentException,
            )
        }
    }

    @Test
    fun `a vendor that issues no refresh token stays refreshless`() {
        val record = ProviderOauthRecord(
            tokenType = "Bearer",
            expiresAtEpochMs = 1,
            scopes = "",
            accessToken = byteArrayOf(1, 2, 3),
            refreshToken = null,
        )

        val decoded = ProviderOauthRecordCodec.decode(ProviderOauthRecordCodec.encode(record))

        requireNotNull(decoded)
        assertNull(decoded.refreshToken)
    }

    @Test
    fun `a raw key is never mistaken for a record`() {
        assertFalse(ProviderOauthRecordCodec.isOauthMaterial("sk-ant-not-a-key".toByteArray()))
        assertNull(ProviderOauthRecordCodec.decode("sk-ant-not-a-key".toByteArray()))
    }

    @Test
    fun `the marker alone is not a record`() {
        val marker = ProviderOauthRecordCodec.MARKER_LINE.toByteArray()
        assertFalse(ProviderOauthRecordCodec.isOauthMaterial(marker))
        assertNull(ProviderOauthRecordCodec.decode(marker + '\n'.code.toByte()))
    }

    @Test
    fun `corrupt records decode to null rather than throwing`() {
        val good = ProviderOauthRecordCodec.encode(
            ProviderOauthRecord(
                tokenType = "Bearer",
                expiresAtEpochMs = 5,
                scopes = "",
                accessToken = byteArrayOf(9),
                refreshToken = null,
            ),
        )
        // Whole-line surgeries a damaged store could produce. Two lines are
        // dropped rather than one, because one short of seven is the six-line
        // shape a record written before the address line has, and that one is
        // a record rather than damage.
        val text = good.decodeToString()
        val mangled = listOf(
            text.replace("Bearer", "Be arer"),
            text.replace("5", "-5"),
            text.lines().dropLast(2).joinToString("\n"),
            text + "\nextra",
        )
        mangled.forEach { corrupted ->
            assertNull(corrupted, ProviderOauthRecordCodec.decode(corrupted.encodeToByteArray()))
        }
    }

    @Test
    fun `bounds are enforced at encode`() {
        val oversized = ProviderOauthRecord(
            tokenType = "Bearer",
            expiresAtEpochMs = 1,
            scopes = "",
            accessToken = ByteArray(5 * 1024) { 1 },
            refreshToken = null,
        )
        assertTrue(
            runCatching { ProviderOauthRecordCodec.encode(oversized) }
                .exceptionOrNull() is IllegalArgumentException,
        )
    }
}
