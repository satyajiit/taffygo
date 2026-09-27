// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.feature.settings.TimeOnSitesRepository
import java.time.Instant
import java.time.ZoneId
import java.time.ZoneOffset

/** Pure, bounded interval ledger used by the profile adapter and host tests. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class TimeOnSitesLedger internal constructor(
    private val segments: MutableList<Segment>,
    private val rollups: MutableList<Rollup>,
    private val zoneId: ZoneId,
    private var recoveredFromCorruption: Boolean,
) {
    data class Segment(val site: String, val startMillis: Long, val endMillis: Long)

    internal data class Rollup(
        val site: String,
        val startMillis: Long,
        val endMillis: Long,
        val durationMillis: Long,
    )

    data class RecentSites(
        val today: List<TimeOnSitesRepository.Site>,
        val week: List<TimeOnSitesRepository.Site>,
    )

    private val namedSites = linkedSetOf<String>()
    private var needsNormalization = true
    private var lastPrunedAtMillis: Long? = null

    init {
        rebuildNamedSites()
    }

    /**
     * Adds measured time without allowing visit cardinality to grow storage.
     *
     * The first [MAX_NAMED_SITES] domains in the retained window stay named.
     * Further domains are explicitly aggregated as "Other sites". Old exact
     * intervals become per-local-day rollups; duration is never discarded to
     * satisfy the segment or preference-size bounds.
     */
    fun append(segment: Segment, nowMillis: Long) {
        if (!segment.isValid()) return
        appendExact(segment.copy(site = boundedSite(segment.site)))
        lastPrunedAtMillis = null
        prune(nowMillis)
    }

    fun prune(nowMillis: Long) {
        if (!needsNormalization && lastPrunedAtMillis == nowMillis) return
        val oldest = saturatedSubtract(nowMillis, RETENTION_MILLIS)
        var identityMayHaveChanged = false
        val exact = segments.listIterator()
        while (exact.hasNext()) {
            val segment = exact.next()
            when {
                segment.endMillis <= oldest || segment.startMillis > nowMillis -> {
                    exact.remove()
                    identityMayHaveChanged = identityMayHaveChanged || segment.site != GROUPED_SITE
                }
                else -> {
                    val retained = segment.copy(
                        startMillis = maxOf(segment.startMillis, oldest),
                        endMillis = minOf(segment.endMillis, nowMillis),
                    )
                    if (retained.isValidInternal()) {
                        exact.set(retained)
                    } else {
                        exact.remove()
                        identityMayHaveChanged =
                            identityMayHaveChanged || segment.site != GROUPED_SITE
                    }
                }
            }
        }
        val rollup = rollups.iterator()
        while (rollup.hasNext()) {
            val candidate = rollup.next()
            // A boundary rollup is removed as one privacy/retention unit. The
            // visible ranges are only seven days; the nine-day ledger margin
            // ensures this never shortens a displayed range.
            if (candidate.startMillis < oldest || candidate.endMillis > nowMillis) {
                rollup.remove()
                identityMayHaveChanged = identityMayHaveChanged || candidate.site != GROUPED_SITE
            }
        }
        if (needsNormalization) mergeExactSegments()
        if (compactOverflowSegments() || needsNormalization) mergeRollups()
        if (identityMayHaveChanged) rebuildNamedSites()
        needsNormalization = false
        lastPrunedAtMillis = nowMillis
    }

    fun clearFrom(cutoffMillis: Long) {
        if (cutoffMillis == Long.MIN_VALUE) {
            segments.clear()
            rollups.clear()
            namedSites.clear()
            lastPrunedAtMillis = null
            return
        }
        val exact = segments.listIterator()
        while (exact.hasNext()) {
            val segment = exact.next()
            when {
                segment.endMillis <= cutoffMillis -> Unit
                segment.startMillis < cutoffMillis -> exact.set(segment.copy(endMillis = cutoffMillis))
                else -> exact.remove()
            }
        }
        // Rollups are deliberately indivisible retention units. If a clear
        // range touches one, remove the whole unit so private history is never
        // retained behind an aggregate.
        rollups.removeAll { it.endMillis > cutoffMillis }
        rebuildNamedSites()
        lastPrunedAtMillis = null
    }

    fun aggregate(fromMillis: Long, untilMillis: Long): List<TimeOnSitesRepository.Site> {
        if (untilMillis <= fromMillis) return emptyList()
        val totals = linkedMapOf<String, Long>()
        for (segment in segments) {
            val start = maxOf(segment.startMillis, fromMillis)
            val end = minOf(segment.endMillis, untilMillis)
            if (end > start) addDuration(totals, segment.site, end - start)
        }
        for (rollup in rollups) {
            // A rollup is used only when the requested range contains all of
            // it. Today and this-week queries start at local-day boundaries,
            // so their measured totals remain exact.
            if (fromMillis <= rollup.startMillis && untilMillis >= rollup.endMillis) {
                addDuration(totals, rollup.site, rollup.durationMillis)
            }
        }
        return totals.toSites()
    }

    /** Builds the two SCR-411 ranges in one pass over the bounded ledger. */
    fun aggregateRecent(
        todayFromMillis: Long,
        weekFromMillis: Long,
        untilMillis: Long,
    ): RecentSites {
        if (untilMillis <= weekFromMillis) return RecentSites(emptyList(), emptyList())
        val todayTotals = linkedMapOf<String, Long>()
        val weekTotals = linkedMapOf<String, Long>()
        for (segment in segments) {
            addOverlap(weekTotals, segment, weekFromMillis, untilMillis)
            addOverlap(todayTotals, segment, todayFromMillis, untilMillis)
        }
        for (rollup in rollups) {
            if (weekFromMillis <= rollup.startMillis && untilMillis >= rollup.endMillis) {
                addDuration(weekTotals, rollup.site, rollup.durationMillis)
            }
            if (todayFromMillis <= rollup.startMillis && untilMillis >= rollup.endMillis) {
                addDuration(todayTotals, rollup.site, rollup.durationMillis)
            }
        }
        return RecentSites(todayTotals.toSites(), weekTotals.toSites())
    }

    private fun Map<String, Long>.toSites(): List<TimeOnSitesRepository.Site> =
        entries.asSequence()
            .filter { it.value > 0L }
            .sortedWith(compareByDescending<Map.Entry<String, Long>> { it.value }.thenBy { it.key })
            .map {
                TimeOnSitesRepository.Site(
                    site = if (it.key == GROUPED_SITE) "" else it.key,
                    durationMillis = it.value,
                    grouped = it.key == GROUPED_SITE,
                )
            }
            .toList()

    fun wasRecoveredFromCorruption(): Boolean = recoveredFromCorruption

    fun encode(): String = TimeOnSitesLedgerCodec.encode(rollups, segments)

    private fun boundedSite(site: String): String {
        if (site == GROUPED_SITE) return site
        if (site in namedSites) return site
        if (namedSites.size >= MAX_NAMED_SITES) return GROUPED_SITE
        namedSites += site
        return site
    }

    private fun rebuildNamedSites() {
        namedSites.clear()
        segments.filter { it.site != GROUPED_SITE }.forEach { namedSites += it.site }
        rollups.filter { it.site != GROUPED_SITE }.forEach { namedSites += it.site }
    }

    private fun appendExact(segment: Segment) {
        var merged = segment
        val iterator = segments.listIterator()
        while (iterator.hasNext()) {
            val candidate = iterator.next()
            if (candidate.site == merged.site && candidate.overlaps(merged)) {
                merged = candidate.union(merged)
                iterator.remove()
            }
        }
        segments += merged
    }

    private fun applyIdentityBound() {
        val allowed = (segments.asSequence().map { it.site to it.startMillis } +
            rollups.asSequence().map { it.site to it.startMillis })
            .filter { it.first != GROUPED_SITE }
            .groupBy({ it.first }, { it.second })
            .mapValues { it.value.minOrNull() ?: Long.MAX_VALUE }
            .entries
            .sortedWith(compareBy<Map.Entry<String, Long>> { it.value }.thenBy { it.key })
            .take(MAX_NAMED_SITES)
            .mapTo(hashSetOf()) { it.key }
        for (index in segments.indices) {
            val segment = segments[index]
            segments[index] = segment.copy(site = segment.site.takeIf(allowed::contains) ?: GROUPED_SITE)
        }
        for (index in rollups.indices) {
            val rollup = rollups[index]
            rollups[index] = rollup.copy(site = rollup.site.takeIf(allowed::contains) ?: GROUPED_SITE)
        }
        rebuildNamedSites()
    }

    private fun mergeExactSegments() {
        val merged = mutableListOf<Segment>()
        for (segment in segments.sortedWith(compareBy<Segment> { it.site }.thenBy { it.startMillis })) {
            val previous = merged.lastOrNull()
            if (previous != null && previous.site == segment.site && previous.endMillis >= segment.startMillis) {
                merged[merged.lastIndex] = previous.copy(endMillis = maxOf(previous.endMillis, segment.endMillis))
            } else {
                merged += segment
            }
        }
        segments.clear()
        segments += merged.sortedWith(compareBy<Segment> { it.startMillis }.thenBy { it.site })
    }

    private fun compactOverflowSegments(): Boolean {
        val overflow = (segments.size - MAX_EXACT_SEGMENTS).coerceAtLeast(0)
        if (overflow == 0) return false

        segments.sortWith(compareBy<Segment> { it.startMillis }.thenBy { it.site })

        // Removing index zero once per entry shifts the whole ArrayList once
        // per removal. A bounded-but-corrupt legacy preference could therefore
        // turn recovery into quadratic work on the browser thread. Copy the
        // exact prefix, clear it in one shift, then roll it up linearly.
        val oldest = segments.subList(0, overflow).toList()
        segments.subList(0, overflow).clear()
        for (segment in oldest) {
            var start = segment.startMillis
            while (start < segment.endMillis) {
                val nextDay = Instant.ofEpochMilli(start)
                    .atZone(zoneId)
                    .toLocalDate()
                    .plusDays(1)
                    .atStartOfDay(zoneId)
                    .toInstant()
                    .toEpochMilli()
                val end = minOf(segment.endMillis, nextDay)
                rollups += Rollup(segment.site, start, end, end - start)
                start = end
            }
        }
        return true
    }

    private fun mergeRollups() {
        val merged = linkedMapOf<Pair<String, Long>, Rollup>()
        for (rollup in rollups) {
            val day = Instant.ofEpochMilli(rollup.startMillis).atZone(zoneId).toLocalDate().toEpochDay()
            val key = rollup.site to day
            val previous = merged[key]
            merged[key] = if (previous == null) {
                rollup
            } else {
                Rollup(
                    site = rollup.site,
                    startMillis = minOf(previous.startMillis, rollup.startMillis),
                    endMillis = maxOf(previous.endMillis, rollup.endMillis),
                    durationMillis = saturatedAdd(previous.durationMillis, rollup.durationMillis),
                )
            }
        }
        rollups.clear()
        rollups += merged.values
    }

    companion object {
        fun decode(
            encoded: String,
            nowMillis: Long,
            zoneId: ZoneId = ZoneOffset.UTC,
        ): TimeOnSitesLedger {
            val ledger = TimeOnSitesLedgerCodec.decode(encoded, zoneId)
            ledger.applyIdentityBound()
            ledger.prune(nowMillis)
            return ledger
        }

        internal const val GROUPED_SITE = "@"
        private const val MAX_EXACT_SEGMENTS = 512
        private const val MAX_NAMED_SITES = 64
        private const val RETENTION_MILLIS = 9L * 24L * 60L * 60L * 1_000L
    }
}

internal fun String.isSafeSite(): Boolean =
    isNotEmpty() && length <= 253 && first() != '.' && last() != '.' && all { byte ->
        (byte in 'a'..'z') || (byte in '0'..'9') || byte == '.' || byte == '-'
    }

internal fun TimeOnSitesLedger.Segment.isValid(): Boolean =
    site.isSafeSite() && startMillis >= 0L && endMillis > startMillis

internal fun TimeOnSitesLedger.Segment.isValidInternal(): Boolean =
    (site.isSafeSite() || site == "@") && startMillis >= 0L && endMillis > startMillis

private fun TimeOnSitesLedger.Segment.overlaps(other: TimeOnSitesLedger.Segment): Boolean =
    endMillis >= other.startMillis && other.endMillis >= startMillis

private fun TimeOnSitesLedger.Segment.union(
    other: TimeOnSitesLedger.Segment,
): TimeOnSitesLedger.Segment = copy(
    startMillis = minOf(startMillis, other.startMillis),
    endMillis = maxOf(endMillis, other.endMillis),
)

private fun addOverlap(
    totals: MutableMap<String, Long>,
    segment: TimeOnSitesLedger.Segment,
    fromMillis: Long,
    untilMillis: Long,
) {
    val start = maxOf(segment.startMillis, fromMillis)
    val end = minOf(segment.endMillis, untilMillis)
    if (end > start) addDuration(totals, segment.site, end - start)
}

private fun addDuration(totals: MutableMap<String, Long>, site: String, duration: Long) {
    totals[site] = saturatedAdd(totals[site] ?: 0L, duration)
}

private fun saturatedAdd(left: Long, right: Long): Long =
    if (right > 0L && left > Long.MAX_VALUE - right) Long.MAX_VALUE else left + right

internal fun saturatedSubtract(left: Long, right: Long): Long =
    if (right > 0L && left < Long.MIN_VALUE + right) Long.MIN_VALUE else left - right
