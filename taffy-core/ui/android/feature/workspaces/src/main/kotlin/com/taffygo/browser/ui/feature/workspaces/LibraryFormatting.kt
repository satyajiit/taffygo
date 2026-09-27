// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.ExportFormat
import java.security.MessageDigest
import java.time.LocalDate
import java.time.format.DateTimeFormatter
import taffy.core_api.LibraryEntryView
import taffy.core_api.WorkspaceExportFormat

/** UTC dates do not become false as wall time advances or a locale changes. */
internal fun libraryDate(epochMillis: ULong): String? {
    if (epochMillis == 0uL) return null
    val epochDay = (epochMillis / LIBRARY_MILLIS_PER_DAY).toLong()
    return LocalDate.ofEpochDay(epochDay).format(DateTimeFormatter.ISO_LOCAL_DATE)
}

internal fun libraryFreshness(epochMillis: ULong): String? =
    libraryDate(epochMillis)?.let { date -> "Checked $date" }

/**
 * The durable model owns only a disagreement bit and active citations, not the
 * competing claim text. Report that bounded fact and do not manufacture the
 * missing claims.
 */
internal fun libraryConflictSummary(entry: LibraryEntryView): String? {
    if (!entry.has_conflict) return null
    return when (val count = entry.sources.size) {
        0 -> "The saved record is marked as conflicting."
        1 -> "1 cited source is marked as conflicting."
        else -> "$count cited sources disagree."
    }
}

internal fun libraryRequestId(
    domain: String,
    revision: ULong,
    collectionId: String?,
    value: String,
): String {
    // The separator is written as an escape rather than as the byte
    // itself. A raw NUL makes Git call this file binary, and a source
    // file Git calls binary has no diff to review and no line for
    // `git grep` to print — the bytes hashed are the same either way.
    val input =
        "$domain\u0000$revision\u0000${collectionId.orEmpty()}\u0000$value"
            .encodeToByteArray()
    val digest = MessageDigest.getInstance("SHA-256").digest(input)
    return buildString(72) {
        append(domain)
        append('-')
        digest.forEach { byte ->
            val value = byte.toInt() and 0xff
            append(HEX[value ushr 4])
            append(HEX[value and 0x0f])
        }
    }
}

internal fun ExportFormat.toCoreFormat(): WorkspaceExportFormat = when (this) {
    ExportFormat.MARKDOWN -> WorkspaceExportFormat.MARKDOWN
    ExportFormat.COMMA_SEPARATED -> WorkspaceExportFormat.CSV
}

internal fun WorkspaceExportFormat.toUiFormat(): ExportFormat = when (this) {
    WorkspaceExportFormat.MARKDOWN -> ExportFormat.MARKDOWN
    WorkspaceExportFormat.CSV -> ExportFormat.COMMA_SEPARATED
}

private const val LIBRARY_MILLIS_PER_DAY = 86_400_000uL
private const val HEX = "0123456789abcdef"
