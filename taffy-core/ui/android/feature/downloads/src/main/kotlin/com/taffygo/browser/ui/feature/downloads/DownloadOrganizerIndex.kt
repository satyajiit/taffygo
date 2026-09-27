// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import java.text.Normalizer
import java.util.Locale

/** Immutable search facts built once per browser snapshot on the processor dispatcher. */
internal class DownloadOrganizerIndex private constructor(
    private val entries: List<IndexedDownload>,
) {
    fun project(
        query: String,
        filter: DownloadFilter,
        sort: DownloadSort,
        grouping: DownloadGrouping,
    ): DownloadOrganizerProjection {
        val terms = normalizedTerms(query)
        val matched = entries.asSequence()
            .filter { entry -> entry.matches(filter) && entry.matches(terms) }
            .toMutableList()
        matched.sortWith(comparator(sort))
        val groups = groups(matched, grouping)
        return DownloadOrganizerProjection(
            groups = groups,
            totalCount = entries.size,
            visibleCount = matched.size,
        )
    }

    companion object {
        fun build(downloads: List<DownloadRecord>): DownloadOrganizerIndex =
            DownloadOrganizerIndex(
                downloads.take(MAX_ORGANIZER_DOWNLOADS).mapIndexed { order, download ->
                    IndexedDownload(
                        download = download,
                        sourceOrder = order,
                        normalizedName = normalized(download.fileName),
                        normalizedHost = normalized(download.host),
                        fileType = fileType(download.fileName),
                    )
                },
            )
    }
}

internal data class DownloadOrganizerProjection(
    val groups: List<DownloadGroup>,
    val totalCount: Int,
    val visibleCount: Int,
)

private data class IndexedDownload(
    val download: DownloadRecord,
    val sourceOrder: Int,
    val normalizedName: String,
    val normalizedHost: String,
    val fileType: DownloadFileType,
) {
    fun matches(filter: DownloadFilter): Boolean = when (filter) {
        DownloadFilter.ALL -> true
        DownloadFilter.ACTIVE -> download.state == DownloadState.RUNNING ||
            download.state == DownloadState.PAUSED
        DownloadFilter.COMPLETE -> download.state == DownloadState.COMPLETE
        DownloadFilter.FAILED -> download.state == DownloadState.FAILED
    }

    fun matches(terms: List<String>): Boolean = terms.all { term ->
        term in normalizedName || term in normalizedHost
    }
}

private fun comparator(sort: DownloadSort): Comparator<IndexedDownload> = when (sort) {
    DownloadSort.NEWEST -> compareBy(IndexedDownload::sourceOrder)
    DownloadSort.NAME -> compareBy(
        IndexedDownload::normalizedName,
        IndexedDownload::normalizedHost,
        IndexedDownload::sourceOrder,
    )
    DownloadSort.LARGEST -> compareByDescending<IndexedDownload> {
        it.download.totalBytes ?: it.download.downloadedBytes
    }.thenBy(IndexedDownload::sourceOrder)
    DownloadSort.SOURCE -> compareBy(
        IndexedDownload::normalizedHost,
        IndexedDownload::normalizedName,
        IndexedDownload::sourceOrder,
    )
}

private fun groups(
    entries: List<IndexedDownload>,
    grouping: DownloadGrouping,
): List<DownloadGroup> = when (grouping) {
    DownloadGrouping.NONE -> if (entries.isEmpty()) {
        emptyList()
    } else {
        listOf(
            DownloadGroup(
                key = "all",
                title = DownloadGroupTitle.All,
                downloads = entries.map { it.download },
            ),
        )
    }
    DownloadGrouping.STATUS -> DownloadState.entries.mapNotNull { state ->
        group(
            key = "state:${state.name}",
            title = DownloadGroupTitle.Status(state),
            entries = entries.filter { it.download.state == state },
        )
    }
    DownloadGrouping.FILE_TYPE -> DownloadFileType.entries.mapNotNull { type ->
        group(
            key = "type:${type.name}",
            title = DownloadGroupTitle.FileType(type),
            entries = entries.filter { it.fileType == type },
        )
    }
    DownloadGrouping.SOURCE -> entries
        .groupBy(IndexedDownload::normalizedHost)
        .toSortedMap()
        .mapNotNull { (normalizedHost, sourceEntries) ->
            val visibleHost = sourceEntries.firstOrNull()?.download?.host.orEmpty()
            group(
                key = "source:$normalizedHost",
                title = DownloadGroupTitle.Source(visibleHost),
                entries = sourceEntries,
            )
        }
}

private fun group(
    key: String,
    title: DownloadGroupTitle,
    entries: List<IndexedDownload>,
): DownloadGroup? = entries.takeIf { it.isNotEmpty() }?.let {
    DownloadGroup(key = key, title = title, downloads = it.map(IndexedDownload::download))
}

internal fun boundedDownloadQuery(value: String): String {
    val codePoints = value.codePointCount(0, value.length)
    if (codePoints <= MAX_DOWNLOAD_QUERY_CODE_POINTS) return value
    return value.substring(0, value.offsetByCodePoints(0, MAX_DOWNLOAD_QUERY_CODE_POINTS))
}

private fun normalizedTerms(query: String): List<String> = normalized(boundedDownloadQuery(query))
    .split(WHITESPACE)
    .filter(String::isNotEmpty)

private fun normalized(value: String): String = Normalizer
    .normalize(value, Normalizer.Form.NFKC)
    .lowercase(Locale.ROOT)

private fun fileType(fileName: String): DownloadFileType {
    val extension = fileName.substringAfterLast('.', missingDelimiterValue = "")
        .lowercase(Locale.ROOT)
        .takeIf { it.length in 1..MAX_EXTENSION_LENGTH }
        ?: return DownloadFileType.OTHER
    return when (extension) {
        "doc", "docx", "epub", "md", "odt", "pdf", "ppt", "pptx", "rtf", "txt", "xls", "xlsx" ->
            DownloadFileType.DOCUMENT
        "avif", "bmp", "gif", "heic", "jpeg", "jpg", "png", "svg", "webp" ->
            DownloadFileType.IMAGE
        "aac", "flac", "m4a", "mp3", "ogg", "opus", "wav" -> DownloadFileType.AUDIO
        "avi", "m4v", "mkv", "mov", "mp4", "webm" -> DownloadFileType.VIDEO
        "7z", "bz2", "gz", "rar", "tar", "tgz", "xz", "zip" -> DownloadFileType.ARCHIVE
        else -> DownloadFileType.OTHER
    }
}

private val WHITESPACE = Regex("\\s+")
private const val MAX_DOWNLOAD_QUERY_CODE_POINTS = 128
private const val MAX_EXTENSION_LENGTH = 16
