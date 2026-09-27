// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.ui.TaffyProjectContact
import java.net.URLDecoder
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ReportAnswerReducerTest {

    private val providers = listOf(ReportAnswerUiState.Provider("SpaceXAI", "Grok 4"))

    @Test
    fun `opening the sheet carries the answer and the providers`() {
        val state = reduceReportAnswer(
            ReportAnswerUiState(),
            ReportAnswerIntent.Open(answer("The price is ", "₹16,999.")),
            providers,
        )

        assertEquals("The price is ₹16,999.", state.report?.answer)
        assertFalse(state.report?.shortened ?: true)
        assertEquals(providers, state.report?.providers)
        assertFalse(state.emailUnavailable)
    }

    @Test
    fun `an answer with no words yet opens nothing`() {
        val closed = ReportAnswerUiState()

        assertEquals(
            closed,
            reduceReportAnswer(closed, ReportAnswerIntent.Open(answer("  ")), providers),
        )
    }

    @Test
    fun `a long answer is cut at the limit and says so`() {
        val limit = ReportAnswerUiState.MAX_REPORTED_ANSWER_CHARS
        val report = reportFor(answer("a".repeat(limit + 10)), providers)

        assertEquals(limit, report?.answer?.length)
        assertTrue(report?.shortened ?: false)
    }

    @Test
    fun `the cut never splits a character in two`() {
        val limit = ReportAnswerUiState.MAX_REPORTED_ANSWER_CHARS
        // An emoji is two UTF-16 units; put one across the limit.
        val text = "a".repeat(limit - 1) + "😀" + "tail"
        val report = reportFor(answer(text), providers)

        assertEquals("a".repeat(limit - 1), report?.answer)
        assertTrue(report?.shortened ?: false)
    }

    @Test
    fun `a draft no email app took keeps the sheet open and names the address`() {
        val open = ReportAnswerUiState(report = reportFor(answer("text"), providers))

        val refused = reduceReportAnswer(open, ReportAnswerIntent.EmailDraft(opened = false), providers)
        assertTrue(refused.emailUnavailable)
        assertEquals(open.report, refused.report)

        val taken = reduceReportAnswer(refused, ReportAnswerIntent.EmailDraft(opened = true), providers)
        assertNull(taken.report)
        assertFalse(taken.emailUnavailable)
    }

    @Test
    fun `an issue or a dismissal closes the sheet`() {
        val open = ReportAnswerUiState(report = reportFor(answer("text"), providers))

        assertNull(
            reduceReportAnswer(open, ReportAnswerIntent.OpenPublicIssue("t"), providers).report,
        )
        assertNull(reduceReportAnswer(open, ReportAnswerIntent.Dismiss, providers).report)
    }

    @Test
    fun `the email body leads with a note and carries answer, providers and version`() {
        val report = ReportAnswerUiState.Report(
            answer = "Flipkart is 16% cheaper.",
            shortened = false,
            providers = listOf(
                ReportAnswerUiState.Provider("SpaceXAI", "Grok 4"),
                ReportAnswerUiState.Provider("My server", null),
            ),
        )

        val body = reportAnswerEmailBody(report, "152.0.7977.42", englishWords, Locale.ENGLISH)

        assertEquals(
            "Your note:\n\n---\nThe answer:\nFlipkart is 16% cheaper.\n\n" +
                "AI providers set up on this phone:\nSpaceXAI, model: Grok 4\n" +
                "My server (no model picked)\n\nTaffyGo version: 152.0.7977.42",
            body,
        )
    }

    @Test
    fun `a cut answer says so and an unknown version is named as unknown`() {
        val report = ReportAnswerUiState.Report(answer = "part", shortened = true, providers = emptyList())

        val body = reportAnswerEmailBody(report, null, englishWords, Locale.ENGLISH)

        assertTrue(body.contains("The answer:\npart\n[Cut here.]"))
        assertTrue(body.contains("AI providers set up on this phone:\nnot known"))
        assertTrue(body.endsWith("TaffyGo version: not known"))
    }

    @Test
    fun `the email draft is addressed to the one mailbox with the answer in its body`() {
        val report = reportFor(answer("Offensive words & more"), providers)!!
        val body = reportAnswerEmailBody(report, "1.0", englishWords, Locale.ENGLISH)

        val uri = TaffyProjectContact.emailDraftUri("TaffyGo: report about an answer", body)

        assertTrue(uri.startsWith("mailto:${TaffyProjectContact.EMAIL}?subject="))
        val decoded = URLDecoder.decode(uri.substringAfter("&body="), Charsets.UTF_8.name())
        assertEquals(body.replace("\n", "\r\n"), decoded)
    }

    @Test
    fun `the public issue carries a title and none of the answer`() {
        val answer = "something the person asked about their bank"
        val address = TaffyProjectContact.newIssueAddress("Report about an answer from Taffy")

        assertTrue(address.startsWith("${TaffyProjectContact.REPOSITORY}/issues/new?"))
        assertFalse(URLDecoder.decode(address, Charsets.UTF_8.name()).contains(answer))
        assertEquals(
            listOf("template", "title"),
            address.substringAfter('?').split('&').map { it.substringBefore('=') },
        )
    }

    private fun answer(vararg segments: String) = TaskAnswerProjection(
        segments = segments.toList(),
        isStreaming = false,
        isIncomplete = false,
        isTruncated = false,
    )

    private val englishWords = ReportAnswerDraftWords(
        body = "Your note:\n\n---\nThe answer:\n%1\$s\n\nAI providers set up on this phone:\n%2\$s" +
            "\n\nTaffyGo version: %3\$s",
        shortened = "%1\$s\n[Cut here.]",
        providerWithModel = "%1\$s, model: %2\$s",
        providerAlone = "%1\$s (no model picked)",
        notKnown = "not known",
    )
}
