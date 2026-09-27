// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import java.util.IdentityHashMap

/** Profile-owned accounting for independently attached browser windows. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class ForegroundTimeSessions(
    private val ledger: TimeOnSitesLedger,
    private val wallClockMillis: () -> Long,
    private val elapsedClockMillis: () -> Long,
) {
    enum class Transition {
        UNCHANGED,
        SESSION_ONLY,
        RECORDED,
    }

    private val activeByWindow = IdentityHashMap<Any, ActiveSite>()

    val hasActiveWindows: Boolean
        get() = activeByWindow.isNotEmpty()

    fun activate(token: Any, site: String?): Transition {
        val current = activeByWindow[token]
        if (current?.site == site) return Transition.UNCHANGED
        val finished = finish(token, restart = false)
        if (site != null) {
            activeByWindow[token] = ActiveSite(site, elapsedClockMillis())
        }
        return if (finished == Transition.RECORDED) finished else Transition.SESSION_ONLY
    }

    fun finish(token: Any, restart: Boolean): Transition =
        finish(token, restart, elapsedClockMillis(), wallClockMillis())

    fun finishAll(restart: Boolean): Transition {
        if (activeByWindow.isEmpty()) return Transition.UNCHANGED
        val nowElapsed = elapsedClockMillis()
        val nowWall = wallClockMillis()
        var result = Transition.SESSION_ONLY
        activeByWindow.keys.toList().forEach { token ->
            if (finish(token, restart, nowElapsed, nowWall) == Transition.RECORDED) {
                result = Transition.RECORDED
            }
        }
        return result
    }

    private fun finish(
        token: Any,
        restart: Boolean,
        nowElapsed: Long,
        nowWall: Long,
    ): Transition {
        val previous = activeByWindow.remove(token) ?: return Transition.UNCHANGED
        val measured = (nowElapsed - previous.elapsedStartMillis)
            .coerceIn(0L, MAX_SINGLE_SEGMENT_MILLIS)
        if (measured > 0L) {
            ledger.append(
                TimeOnSitesLedger.Segment(
                    previous.site,
                    saturatedSubtract(nowWall, measured),
                    nowWall,
                ),
                nowWall,
            )
        }
        if (restart) {
            activeByWindow[token] = previous.copy(elapsedStartMillis = nowElapsed)
        }
        return if (measured > 0L) Transition.RECORDED else Transition.SESSION_ONLY
    }

    private data class ActiveSite(
        val site: String,
        val elapsedStartMillis: Long,
    )

    private companion object {
        const val MAX_SINGLE_SEGMENT_MILLIS = 24L * 60L * 60L * 1_000L
    }
}
