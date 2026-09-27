// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.os.Handler
import com.taffygo.browser.ui.app.ProfilePreferenceStore
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.mockito.Mockito.`when`
import org.chromium.chrome.browser.profiles.Profile

/** The pure profile ledger is the tight host loop for SCR-411's measured facts. */
@RunWith(BaseRobolectricTestRunner::class)
class ChromiumTimeOnSitesRepositoryTest {
    @Test
    fun adjacentCheckpointsRemainOneVisitAcrossAWeek() {
        val ledger = TimeOnSitesLedger.decode("", 0L)
        val minute = 60_000L
        repeat(7 * 24 * 60) { index ->
            val start = index * minute
            ledger.append(
                TimeOnSitesLedger.Segment("example.test", start, start + minute),
                start + minute,
            )
        }

        val encoded = ledger.encode()
        val restored = TimeOnSitesLedger.decode(encoded, 7 * 24 * 60 * minute)

        assertEquals(2, encoded.lineSequence().filter(String::isNotEmpty).count())
        assertEquals(
            7 * 24 * 60 * minute,
            restored.aggregate(0L, 7 * 24 * 60 * minute).single().durationMillis,
        )
    }

    @Test
    fun preferenceEncodingNeverOutgrowsItsOwnDecoder() {
        val ledger = TimeOnSitesLedger.decode("", 0L)
        repeat(4_096) { index ->
            val start = index * 1_000L
            val site = "$index.${"a".repeat(240)}"
            ledger.append(
                TimeOnSitesLedger.Segment(site, start, start + 1_000L),
                start + 1_000L,
            )
        }

        val encoded = ledger.encode()
        val restored = TimeOnSitesLedger.decode(encoded, 4_096_000L)

        assertTrue(encoded.encodeToByteArray().size <= 512 * 1_024)
        assertTrue(restored.aggregate(0L, 4_096_000L).isNotEmpty())
    }

    @Test
    fun `more than four thousand visits retain every measured millisecond`() {
        val ledger = TimeOnSitesLedger.decode("", 0L)
        repeat(4_097) { index ->
            val start = index * 2_000L
            ledger.append(
                TimeOnSitesLedger.Segment("site-$index.test", start, start + 1_000L),
                start + 1_000L,
            )
        }

        val until = 4_097 * 2_000L
        val beforeRestart = ledger.aggregate(0L, until).sumOf { it.durationMillis }
        val restored = TimeOnSitesLedger.decode(ledger.encode(), until)
        val afterRestart = restored.aggregate(0L, until).sumOf { it.durationMillis }

        assertEquals(4_097_000L, beforeRestart)
        assertEquals(beforeRestart, afterRestart)
        assertTrue(restored.aggregate(0L, until).single { it.grouped }.site.isEmpty())
    }

    @Test
    fun `long site names compact without losing measured time`() {
        val ledger = TimeOnSitesLedger.decode("", 0L)
        repeat(2_000) { index ->
            val start = index * 2_000L
            val site = "$index.${"a".repeat(240)}"
            ledger.append(
                TimeOnSitesLedger.Segment(site, start, start + 1_000L),
                start + 1_000L,
            )
        }

        val until = 2_000 * 2_000L
        val encoded = ledger.encode()
        val restored = TimeOnSitesLedger.decode(encoded, until)

        assertTrue(encoded.encodeToByteArray().size <= 512 * 1_024)
        assertEquals(2_000_000L, restored.aggregate(0L, until).sumOf { it.durationMillis })
    }

    @Test
    fun clearingARecentRangeKeepsOnlyTheOlderOverlap() {
        val ledger = TimeOnSitesLedger.decode("", 0L)
        ledger.append(TimeOnSitesLedger.Segment("example.test", 1_000L, 5_000L), 5_000L)

        ledger.clearFrom(3_000L)

        assertEquals(
            2_000L,
            ledger.aggregate(0L, 10_000L).single().durationMillis,
        )
    }

    @Test
    fun recentRangesShareOneLedgerPassWithoutChangingTheirTotals() {
        val ledger = TimeOnSitesLedger.decode("", 0L)
        ledger.append(TimeOnSitesLedger.Segment("old.test", 1_000L, 2_000L), 2_000L)
        ledger.append(TimeOnSitesLedger.Segment("today.test", 3_000L, 5_000L), 5_000L)

        val recent = ledger.aggregateRecent(
            todayFromMillis = 2_500L,
            weekFromMillis = 0L,
            untilMillis = 5_000L,
        )

        assertEquals(ledger.aggregate(2_500L, 5_000L), recent.today)
        assertEquals(ledger.aggregate(0L, 5_000L), recent.week)
    }

    @Test
    fun malformedOrNonHostPreferenceRowsAreIgnored() {
        val invalid = listOf(
            "not a host",
            ".example.test",
            "example.test.",
            "example.test/path",
            "example\ttest",
        )
        val encoded = buildString {
            append("v1\n")
            for (site in invalid) {
                val value = java.util.Base64.getUrlEncoder().withoutPadding()
                    .encodeToString(site.encodeToByteArray())
                append("1\t2\t$value\n")
            }
        }

        val restored = TimeOnSitesLedger.decode(encoded, 2L)

        assertTrue(restored.aggregate(0L, 3L).isEmpty())
        assertTrue(restored.wasRecoveredFromCorruption())
    }

    @Test
    fun corruptionIsReportedAndTheNextRestartUsesTheRecoveredLedger() {
        val recovered = TimeOnSitesLedger.decode("v2\nnot-a-row\n", 2L)

        assertTrue(recovered.wasRecoveredFromCorruption())
        assertTrue(recovered.aggregate(0L, 3L).isEmpty())
        assertFalse(TimeOnSitesLedger.decode(recovered.encode(), 2L).wasRecoveredFromCorruption())
    }

    @Test
    fun nonAsciiCorruptRowDoesNotDiscardTheValidBoundedRowsAroundIt() {
        val recovered = TimeOnSitesLedger.decode(
            "v2\nv\t1\t2\tZXhhbXBsZS50ZXN0\n\u2603\n",
            2L,
        )

        assertTrue(recovered.wasRecoveredFromCorruption())
        assertEquals(1L, recovered.aggregate(0L, 3L).single().durationMillis)
    }

    @Test
    fun oversizedLegacyValueIsReportedInsteadOfSilentlyTruncated() {
        val oversized = "v2\n" + "x".repeat(512 * 1_024)

        val recovered = TimeOnSitesLedger.decode(oversized, 2L)

        assertTrue(recovered.wasRecoveredFromCorruption())
        assertTrue(recovered.aggregate(0L, 3L).isEmpty())
        assertFalse(TimeOnSitesLedger.decode(recovered.encode(), 2L).wasRecoveredFromCorruption())
    }

    @Test
    fun retentionDropsOnlyTimeOutsideTheNineDayLedgerWindow() {
        val day = 24L * 60L * 60L * 1_000L
        val now = 10L * day
        val ledger = TimeOnSitesLedger.decode("", 0L)
        ledger.append(TimeOnSitesLedger.Segment("old.test", 0L, day), day)
        ledger.append(TimeOnSitesLedger.Segment("kept.test", 2L * day, 3L * day), now)

        assertEquals(listOf("kept.test"), ledger.aggregate(0L, now).map { it.site })
    }

    @Test
    fun attachedWindowsKeepIndependentForegroundSessions() {
        var now = 0L
        val ledger = TimeOnSitesLedger.decode("", now)
        val sessions = ForegroundTimeSessions(ledger, { now }, { now })
        val firstWindow = Any()
        val secondWindow = Any()

        sessions.activate(firstWindow, "first.test")
        sessions.activate(secondWindow, "second.test")
        now = 1_000L
        sessions.finish(firstWindow, restart = false)
        now = 2_000L
        sessions.finish(secondWindow, restart = false)

        assertEquals(3_000L, ledger.aggregate(0L, now).sumOf { it.durationMillis })
    }

    @Test
    fun overlappingWindowsOnTheSameSiteAreCountedOnce() {
        var now = 0L
        val ledger = TimeOnSitesLedger.decode("", now)
        val sessions = ForegroundTimeSessions(ledger, { now }, { now })
        val firstWindow = Any()
        val secondWindow = Any()

        sessions.activate(firstWindow, "same.test")
        sessions.activate(secondWindow, "same.test")
        now = 1_000L
        sessions.finish(firstWindow, restart = false)
        now = 2_000L
        sessions.finish(secondWindow, restart = false)

        assertEquals(2_000L, ledger.aggregate(0L, now).single().durationMillis)
    }

    @Test
    fun navigationBurstsScheduleOneLedgerWriteAndLifecycleFlushesIt() {
        val scheduled = mutableListOf<Runnable>()
        val cancelled = mutableListOf<Runnable>()
        var writes = 0
        val writer = CoalescedTimeOnSitesWriter(
            schedule = { task, _ -> scheduled.add(task) },
            cancel = cancelled::add,
            write = { writes += 1 },
        )

        repeat(100) { writer.request() }

        assertEquals(1, scheduled.size)
        assertEquals(0, writes)
        writer.flush()
        assertEquals(listOf(scheduled.single()), cancelled)
        assertEquals(1, writes)

        scheduled.single().run()
        assertEquals(1, writes)
    }

    @Test
    fun aRejectedScheduleFallsBackToAnImmediateLedgerWrite() {
        var writes = 0
        val writer = CoalescedTimeOnSitesWriter(
            schedule = { _, _ -> false },
            cancel = {},
            write = { writes += 1 },
        )

        writer.request()

        assertEquals(1, writes)
    }

    @Test
    fun closingACleanWriterDoesNotRepeatPersistence() {
        var writes = 0
        val writer = CoalescedTimeOnSitesWriter(
            schedule = { _, _ -> true },
            cancel = {},
            write = { writes += 1 },
        )

        writer.close()

        assertEquals(0, writes)
    }

    @Test
    fun sessionTransitionsDoNotClaimThatStartingAClockRecordedTime() {
        var now = 0L
        val ledger = TimeOnSitesLedger.decode("", now)
        val sessions = ForegroundTimeSessions(ledger, { now }, { now })
        val window = Any()

        assertEquals(
            ForegroundTimeSessions.Transition.UNCHANGED,
            sessions.activate(window, null),
        )
        assertEquals(
            ForegroundTimeSessions.Transition.SESSION_ONLY,
            sessions.activate(window, "first.test"),
        )
        now = 1_000L
        assertEquals(
            ForegroundTimeSessions.Transition.RECORDED,
            sessions.activate(window, "second.test"),
        )
        assertEquals(1_000L, ledger.aggregate(0L, now).single().durationMillis)
    }

    @Test
    fun privateProfileIsRejectedBeforePreferencesAreRead() {
        val profile = mock(Profile::class.java)
        val handler = mock(Handler::class.java)
        val preferences = CountingPreferences()
        `when`(profile.isOffTheRecord).thenReturn(true)

        var rejected = false
        try {
            ChromiumTimeOnSitesRepository(profile, preferences, handler = handler)
        } catch (_: IllegalArgumentException) {
            rejected = true
        }

        assertTrue(rejected)
        assertEquals(0, preferences.reads)
    }

    private class CountingPreferences : ProfilePreferenceStore {
        var reads = 0

        override fun getString(name: String): String {
            reads += 1
            return ""
        }

        override fun putString(name: String, value: String) = Unit
        override fun getBoolean(name: String): Boolean = false
        override fun putBoolean(name: String, value: Boolean) = Unit
    }
}
