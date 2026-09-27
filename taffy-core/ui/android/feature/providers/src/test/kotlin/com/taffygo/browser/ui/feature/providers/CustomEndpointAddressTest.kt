// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The address rules of decision 0096 section 3, proved without a network.
 *
 * What this file no longer holds is the correction. A bare origin probes green
 * and then fails every request, because the prober identifies a runtime at
 * `<origin>/api/tags` or `<origin>/props` while the transport joins only the
 * operation beneath the address a person typed — and until Core API 3.18 this
 * screen had to work the corrected address out from the typed one and the
 * runtime named. The probe now says which base it proved, so the rule lives in
 * `CustomEndpointProjection.proposalFor` and is tested there against an answer
 * rather than against a guess.
 */
class CustomEndpointAddressTest {

    @Test
    fun `every state a person types through is answered rather than thrown on`() {
        // This screen re-projects on each keystroke, so an address is not one
        // input: it is every prefix of itself. `http://` and `http:/` parse
        // and carry no host, and reading that host into a non-null parameter
        // threw NullPointerException on the main thread, which on a phone is
        // the whole browser process going away while somebody types. Driving
        // the prefixes is what catches this class; asserting on the finished
        // address never could.
        for (typed in listOf(
            "http://localhost:8099/v1",
            "https://models.example.test/v1",
            "http://192.168.1.9:11434/v1",
        )) {
            for (length in 0..typed.length) {
                val prefix = typed.substring(0, length)
                // Each of the three must answer for every prefix. The values
                // are not asserted — a prefix has no right answer — only that
                // asking is survivable.
                CustomEndpointAddress.classify(prefix)
                CustomEndpointAddress.isCleartextToLocal(prefix)
                CustomEndpointAddress.hostOf(prefix)
            }
        }
    }

    @Test
    fun `an address with no host names no local machine`() {
        assertFalse(CustomEndpointAddress.isCleartextToLocal("http://"))
        assertFalse(CustomEndpointAddress.isCleartextToLocal("http:/"))
        assertFalse(CustomEndpointAddress.isCleartextToLocal("http:"))
        assertTrue(CustomEndpointAddress.isCleartextToLocal("http://localhost:8099/v1"))
    }

    @Test
    fun `https reaches anything and http reaches only a literal local machine`() {
        assertNull(CustomEndpointAddress.classify("https://models.example.test/v1"))
        assertNull(CustomEndpointAddress.classify("http://localhost:11434/v1"))
        assertNull(CustomEndpointAddress.classify("http://127.0.0.1:11434/v1"))
        assertNull(CustomEndpointAddress.classify("http://192.168.1.9:11434/v1"))
        assertNull(CustomEndpointAddress.classify("http://10.0.0.4:8000/v1"))
        assertNull(CustomEndpointAddress.classify("http://172.16.3.3:8000/v1"))
        assertNull(CustomEndpointAddress.classify("http://desktop.local:1234/v1"))
        assertNull(CustomEndpointAddress.classify("http://[fd12:3456::1]:8080/v1"))
        assertEquals(
            CustomEndpointAddress.Refusal.CLEARTEXT_NOT_LOCAL,
            CustomEndpointAddress.classify("http://models.example.test/v1"),
        )
        assertEquals(
            CustomEndpointAddress.Refusal.CLEARTEXT_NOT_LOCAL,
            CustomEndpointAddress.classify("http://8.8.8.8/v1"),
        )
    }

    @Test
    fun `a name that resolves privately is still a name, and is refused cleartext`() {
        // The whole reason decision 0096 admits literals and refuses names:
        // resolution happens after the check and the answer can change between
        // them, so a name accepted on one lookup can be repointed at anything.
        assertEquals(
            CustomEndpointAddress.Refusal.CLEARTEXT_NOT_LOCAL,
            CustomEndpointAddress.classify("http://my-nas.example.test:11434/v1"),
        )
    }

    @Test
    fun `the instance-metadata addresses are refused although their ranges are admitted`() {
        assertTrue(CustomEndpointAddress.classify("http://169.254.1.2:11434/v1") == null)
        assertEquals(
            CustomEndpointAddress.Refusal.CLEARTEXT_NOT_LOCAL,
            CustomEndpointAddress.classify("http://169.254.169.254/v1"),
        )
        assertEquals(
            CustomEndpointAddress.Refusal.CLEARTEXT_NOT_LOCAL,
            CustomEndpointAddress.classify("http://[fd00:ec2::254]/v1"),
        )
    }

    @Test
    fun `credentials, a query and a fragment each get their own refusal`() {
        assertEquals(
            CustomEndpointAddress.Refusal.CARRIES_CREDENTIALS,
            CustomEndpointAddress.classify("https://me:secret@models.example.test/v1"),
        )
        assertEquals(
            CustomEndpointAddress.Refusal.CARRIES_QUERY,
            CustomEndpointAddress.classify("https://models.example.test/v1?key=1"),
        )
        assertEquals(
            CustomEndpointAddress.Refusal.CARRIES_FRAGMENT,
            CustomEndpointAddress.classify("https://models.example.test/v1#here"),
        )
        assertEquals(
            CustomEndpointAddress.Refusal.NOT_AN_ADDRESS,
            CustomEndpointAddress.classify("ftp://models.example.test/v1"),
        )
        assertEquals(
            CustomEndpointAddress.Refusal.NOT_AN_ADDRESS,
            CustomEndpointAddress.classify("192.168.1.9:11434"),
        )
        assertEquals(
            CustomEndpointAddress.Refusal.TOO_LONG,
            CustomEndpointAddress.classify("https://a.test/" + "v".repeat(600)),
        )
        assertEquals(
            CustomEndpointAddress.Refusal.TOO_LONG,
            CustomEndpointAddress.classify("https://a.test/" + "🧁".repeat(140)),
        )
    }

    @Test
    fun `cleartext to a local machine is admitted and still said out loud`() {
        assertTrue(CustomEndpointAddress.isCleartextToLocal("http://192.168.1.9:11434/v1"))
        assertFalse(CustomEndpointAddress.isCleartextToLocal("https://192.168.1.9:11434/v1"))
        assertFalse(CustomEndpointAddress.isCleartextToLocal("http://models.example.test/v1"))
    }

    @Test
    fun `every preset carries the version segment the transport needs`() {
        CustomEndpointAddress.Preset.entries.forEach { preset ->
            assertNull(preset.address, CustomEndpointAddress.classify(preset.address))
            assertTrue(preset.address, preset.address.endsWith("/v1"))
        }
    }

    /**
     * The machine, read off the address, so an identity can be minted before a
     * person has said what to call the provider.
     */
    @Test
    fun `the host is read off an address and nothing is read off a non-address`() {
        assertEquals("192.168.1.9", CustomEndpointAddress.hostOf("http://192.168.1.9:11434/v1"))
        assertEquals("desktop.local", CustomEndpointAddress.hostOf(" http://desktop.local:1234 "))
        assertNull(CustomEndpointAddress.hostOf("not an address"))
        assertNull(CustomEndpointAddress.hostOf(""))
    }
}
