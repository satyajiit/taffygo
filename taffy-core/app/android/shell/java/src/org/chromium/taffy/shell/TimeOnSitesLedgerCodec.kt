// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import java.time.ZoneId
import java.util.Base64
import java.util.Locale

/** Streaming persistence codec for the bounded time-on-sites ledger. */
internal object TimeOnSitesLedgerCodec {
    fun encode(
        rollups: List<TimeOnSitesLedger.Rollup>,
        segments: List<TimeOnSitesLedger.Segment>,
    ): String = buildString {
        append(VERSION)
        append('\n')
        val orderedRollups = rollups.sortedWith(
            compareBy<TimeOnSitesLedger.Rollup> { it.startMillis }.thenBy { it.site },
        )
        for (rollup in orderedRollups) {
            append("r\t")
            append(rollup.startMillis)
            append('\t')
            append(rollup.endMillis)
            append('\t')
            append(rollup.durationMillis)
            append('\t')
            append(encodeSite(rollup.site))
            append('\n')
        }
        val orderedSegments = segments.sortedWith(
            compareBy<TimeOnSitesLedger.Segment> { it.startMillis }.thenBy { it.site },
        )
        for (segment in orderedSegments) {
            append("v\t")
            append(segment.startMillis)
            append('\t')
            append(segment.endMillis)
            append('\t')
            append(encodeSite(segment.site))
            append('\n')
        }
    }.also { encoded ->
        // The grammar is ASCII-only, so character count is byte count without
        // allocating a second half-megabyte byte array on the browser thread.
        check(encoded.length <= MAX_ENCODED_LENGTH) {
            "Bounded time-on-sites ledger exceeded its persistence budget"
        }
    }

    fun decode(encoded: String, zoneId: ZoneId): TimeOnSitesLedger {
        if (encoded.isEmpty()) return empty(zoneId)
        if (!encoded.fitsUtf8ByteLimit(MAX_ENCODED_LENGTH)) {
            return empty(zoneId, recovered = true)
        }
        val lines = encoded.lineSequence().iterator()
        val version = if (lines.hasNext()) lines.next() else null
        return when (version) {
            LEGACY_VERSION -> decodeLegacy(lines.asSequence(), zoneId)
            VERSION -> decodeCurrent(lines.asSequence(), zoneId)
            else -> empty(zoneId, recovered = true)
        }
    }

    private fun decodeLegacy(lines: Sequence<String>, zoneId: ZoneId): TimeOnSitesLedger {
        var corrupt = false
        val segments = lines.mapNotNull { line ->
            if (line.isEmpty()) return@mapNotNull null
            val parts = line.split('\t', limit = 3)
            val decoded = if (parts.size == 3) {
                decodeSite(parts[2])?.let { site ->
                    TimeOnSitesLedger.Segment(
                        site,
                        parts[0].toLongOrNull() ?: -1L,
                        parts[1].toLongOrNull() ?: -1L,
                    )
                }?.takeIf(TimeOnSitesLedger.Segment::isValid)
            } else {
                null
            }
            if (decoded == null) corrupt = true
            decoded
        }.toMutableList()
        return TimeOnSitesLedger(segments, mutableListOf(), zoneId, corrupt)
    }

    private fun decodeCurrent(lines: Sequence<String>, zoneId: ZoneId): TimeOnSitesLedger {
        val segments = mutableListOf<TimeOnSitesLedger.Segment>()
        val rollups = mutableListOf<TimeOnSitesLedger.Rollup>()
        var corrupt = false
        for (line in lines) {
            if (line.isEmpty()) continue
            val parts = line.split('\t')
            val accepted = when (parts.firstOrNull()) {
                "v" -> if (parts.size == 4) {
                    decodeStoredSite(parts[3])?.let { site ->
                        TimeOnSitesLedger.Segment(
                            site,
                            parts[1].toLongOrNull() ?: -1L,
                            parts[2].toLongOrNull() ?: -1L,
                        )
                    }?.takeIf(TimeOnSitesLedger.Segment::isValidInternal)
                        ?.also(segments::add) != null
                } else {
                    false
                }
                "r" -> if (parts.size == 5) {
                    decodeStoredSite(parts[4])?.let { site ->
                        TimeOnSitesLedger.Rollup(
                            site,
                            parts[1].toLongOrNull() ?: -1L,
                            parts[2].toLongOrNull() ?: -1L,
                            parts[3].toLongOrNull() ?: -1L,
                        )
                    }?.takeIf(::isValidRollup)?.also(rollups::add) != null
                } else {
                    false
                }
                else -> false
            }
            if (!accepted) corrupt = true
        }
        return TimeOnSitesLedger(segments, rollups, zoneId, corrupt)
    }

    private fun empty(zoneId: ZoneId, recovered: Boolean = false) =
        TimeOnSitesLedger(mutableListOf(), mutableListOf(), zoneId, recovered)

    private fun encodeSite(site: String): String =
        if (site == TimeOnSitesLedger.GROUPED_SITE) {
            GROUPED_ENCODING
        } else {
            Base64.getUrlEncoder().withoutPadding().encodeToString(site.encodeToByteArray())
        }

    private fun decodeStoredSite(value: String): String? =
        if (value == GROUPED_ENCODING) TimeOnSitesLedger.GROUPED_SITE else decodeSite(value)

    private fun decodeSite(value: String): String? = runCatching {
        Base64.getUrlDecoder().decode(value).decodeToString().lowercase(Locale.ROOT)
    }.getOrNull()?.takeIf(String::isSafeSite)

    private fun isValidRollup(rollup: TimeOnSitesLedger.Rollup): Boolean =
        (rollup.site.isSafeSite() || rollup.site == TimeOnSitesLedger.GROUPED_SITE) &&
            rollup.startMillis >= 0L &&
            rollup.endMillis > rollup.startMillis &&
            rollup.durationMillis in 1..(rollup.endMillis - rollup.startMillis)

    private const val VERSION = "v2"
    private const val LEGACY_VERSION = "v1"
    private const val GROUPED_ENCODING = "-"
    private const val MAX_ENCODED_LENGTH = 512 * 1_024
}

/** Measures the persistence budget without materializing a second byte array. */
private fun String.fitsUtf8ByteLimit(limit: Int): Boolean {
    var bytes = 0
    var index = 0
    while (index < length) {
        val character = this[index]
        val width = when {
            character <= '\u007f' -> 1
            character <= '\u07ff' -> 2
            Character.isHighSurrogate(character) &&
                index + 1 < length && Character.isLowSurrogate(this[index + 1]) -> {
                index += 1
                4
            }
            else -> 3
        }
        if (bytes > limit - width) return false
        bytes += width
        index += 1
    }
    return true
}
