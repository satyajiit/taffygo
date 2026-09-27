// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import java.util.PriorityQueue

/**
 * Bounded k-way merge over the per-source inverted index.
 *
 * Each source list is already in entry order. Merging only until [limit]
 * distinct neighbours have been found preserves that order while touching a
 * small bounded prefix even when every entry shares a source.
 */
internal fun relatedLibraryEntryIndices(
    sourcesByEntry: List<List<String>>,
    limit: Int,
): List<List<Int>> {
    if (limit <= 0) return List(sourcesByEntry.size) { emptyList() }
    val normalized = ArrayList<List<String>>(sourcesByEntry.size)
    val entriesBySource = HashMap<String, MutableList<Int>>()
    sourcesByEntry.forEachIndexed { entryIndex, sourceIds ->
        val distinct = LinkedHashSet<String>(sourceIds.size)
        for (sourceId in sourceIds) {
            if (sourceId.isNotBlank()) distinct += sourceId
        }
        normalized += distinct.toList()
        for (sourceId in distinct) {
            entriesBySource.getOrPut(sourceId, ::ArrayList) += entryIndex
        }
    }
    return normalized.mapIndexed { entryIndex, sourceIds ->
        val queue = PriorityQueue<RelatedCursor>(
            compareBy<RelatedCursor> { it.entryIndex }.thenBy { it.sourceOrdinal },
        )
        sourceIds.forEachIndexed { sourceOrdinal, sourceId ->
            val indices = entriesBySource[sourceId].orEmpty()
            if (indices.isNotEmpty()) queue += RelatedCursor(sourceOrdinal, indices, 0)
        }
        val related = ArrayList<Int>(limit)
        var lastCandidate = -1
        while (queue.isNotEmpty() && related.size < limit) {
            val cursor = queue.remove()
            val candidate = cursor.entryIndex
            if (candidate != lastCandidate) {
                lastCandidate = candidate
                if (candidate != entryIndex) related += candidate
            }
            cursor.next()?.let(queue::add)
        }
        related
    }
}

private data class RelatedCursor(
    val sourceOrdinal: Int,
    val entries: List<Int>,
    val position: Int,
) {
    val entryIndex: Int
        get() = entries[position]

    fun next(): RelatedCursor? = if (position + 1 < entries.size) {
        copy(position = position + 1)
    } else {
        null
    }
}
