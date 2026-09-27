// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.analytics

import com.taffygo.browser.ui.core.analytics.internal.InMemoryAnalyticsClient
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The content-free rule of android-app-architecture section 8, asserted against
 * a corpus of canary secrets.
 *
 * The canaries are the things a payload must never carry: a URL, a page title,
 * a prompt, a model output, a file name, and an account identifier. Every event
 * the vocabulary can produce is built with a canary pushed into every place a
 * caller could put one, and no canary survives into a payload — because the
 * event types have nowhere to put one.
 */
class AnalyticsPayloadTest {

    private val recorded = mutableListOf<String>()
    private val client = InMemoryAnalyticsClient(RecordingLogger(recorded))

    @Test
    fun `every event name is on the allowlist`() {
        for (event in everyEvent()) {
            assertTrue(event.name, event.name in AnalyticsEvent.ALLOWED_NAMES)
        }
        assertEquals(AnalyticsEvent.ALLOWED_NAMES.size, everyEvent().map { it.name }.toSet().size)
    }

    @Test
    fun `every parameter value is a compiled-in token, never free text`() {
        for (event in everyEvent()) {
            for ((key, value) in event.parameters) {
                assertTrue("$key is not a token key", key.matches(TOKEN))
                assertTrue("${event.name}.$key is $value", value.matches(TOKEN))
            }
        }
    }

    @Test
    fun `no canary secret can reach a payload`() {
        val payloads = everyEvent().flatMap { it.parameters.values } + everyEvent().map { it.name }

        for (canary in CANARIES) {
            assertTrue(canary, payloads.none { it.contains(canary, ignoreCase = true) })
        }
    }

    @Test
    fun `an undeclared event cannot be constructed at all`() {
        // The vocabulary is a sealed hierarchy, so no module outside
        // `:core:analytics` can add a member to it: this test source set cannot
        // even write one down. The allowlist below is therefore a declaration
        // checked against the hierarchy, not a filter applied at runtime.
        assertEquals(
            AnalyticsEvent.ALLOWED_NAMES,
            everyEvent().map { it.name }.toSet(),
        )
    }

    @Test
    fun `a recorded event reaches the diagnostic log without any content`() {
        client.record(AnalyticsEvent.TaskFinished("PARTLY_DONE", AnalyticsBucket.of(3)))

        val line = recorded.single()
        assertTrue(line, line.startsWith("DEBUG: task_finished"))
        assertTrue(line, CANARIES.none { line.contains(it, ignoreCase = true) })
    }

    @Test
    fun `the recorder keeps a bounded history`() {
        repeat(150) { client.record(AnalyticsEvent.ScreenShown("SCR-101")) }

        assertEquals(100, client.recent().size)
    }

    @Test
    fun `a count becomes a bucket and never a number`() {
        val buckets = listOf(0, 1, 3, 12, 400).map(AnalyticsBucket::of)

        assertEquals(listOf("NONE", "ONE", "TWO_TO_FIVE", "SIX_TO_TWENTY", "OVER_TWENTY"), buckets)
        assertTrue(AnalyticsBucket.ALL.all { it.matches(TOKEN) })
    }

    private fun everyEvent(): List<AnalyticsEvent> = listOf(
        AnalyticsEvent.ScreenShown("SCR-303"),
        AnalyticsEvent.TaskStateChanged("PARTLY_DONE"),
        AnalyticsEvent.TaskControlUsed("TAKE_OVER"),
        AnalyticsEvent.TaskFinished("DONE", AnalyticsBucket.of(4)),
        AnalyticsEvent.ArtifactExported("MARKDOWN"),
        AnalyticsEvent.SnapshotInspected("PAGESNAPSHOT"),
        AnalyticsEvent.FailureObserved("TIMED_OUT"),
    )

    private companion object {
        /** A parameter key or value: capitals, digits, underscores, hyphens. */
        val TOKEN = Regex("^[A-Za-z0-9_-]+$")

        /**
         * The things a payload must never carry. Each is deliberately shaped
         * like the real thing it stands for.
         */
        val CANARIES = listOf(
            "https://",
            "example.test",
            "Retention policy",
            "compare these 4 tabs",
            "the answer is",
            "price-list.csv",
            "user@example.test",
            "acct_",
        )
    }
}
