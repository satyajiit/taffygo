// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import java.net.URLDecoder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TaffyProjectContactTest {

    @Test
    fun `the email draft is addressed to the one mailbox and carries subject and body`() {
        val uri = TaffyProjectContact.emailDraftUri(subject = "A subject", body = "Line one")

        assertEquals(
            "mailto:${TaffyProjectContact.EMAIL}?subject=A%20subject&body=Line%20one",
            uri,
        )
    }

    @Test
    fun `a line break in the body is written the way RFC 6068 asks`() {
        val uri = TaffyProjectContact.emailDraftUri(subject = "s", body = "one\ntwo\r\nthree")

        assertTrue(uri.endsWith("&body=one%0D%0Atwo%0D%0Athree"))
    }

    @Test
    fun `characters that mean something in an address cannot break out of their field`() {
        val body = "a+b & c=d ? e # f % g"
        val uri = TaffyProjectContact.emailDraftUri(subject = "x&cc=someone", body = body)
        val query = uri.substringAfter('?')
        val fields = query.split('&')

        assertEquals(listOf("subject", "body"), fields.map { it.substringBefore('=') })
        assertEquals("x&cc=someone", decode(fields[0].substringAfter('=')))
        assertEquals(body, decode(fields[1].substringAfter('=')))
        assertFalse(query.contains('+'))
    }

    @Test
    fun `text in another script survives as UTF-8`() {
        val body = "यह जवाब ठीक नहीं था"
        val uri = TaffyProjectContact.emailDraftUri(subject = "s", body = body)

        assertEquals(body, decode(uri.substringAfter("&body=")))
    }

    @Test
    fun `a public issue carries its title and nothing else`() {
        val address = TaffyProjectContact.newIssueAddress("Report about an answer from Taffy")

        assertEquals(
            "${TaffyProjectContact.REPOSITORY}/issues/new" +
                "?template=bug_report.yml&title=Report%20about%20an%20answer%20from%20Taffy",
            address,
        )
    }

    @Test
    fun `the issue pages live under the one repository address`() {
        assertTrue(TaffyProjectContact.REPOSITORY.startsWith("https://github.com/"))
        assertTrue(TaffyProjectContact.ISSUE_CHOOSER.startsWith(TaffyProjectContact.REPOSITORY))
        assertTrue(
            TaffyProjectContact.newIssueAddress("t").startsWith(TaffyProjectContact.REPOSITORY),
        )
    }

    private fun decode(value: String): String =
        URLDecoder.decode(value.replace("+", "%2B"), Charsets.UTF_8.name())
}
