// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.AddressBarPageSuggestion
import com.taffygo.browser.ui.core.browser.AddressBarSuggestionSource
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import java.net.URI

/** Immutable bounded prefix index rebuilt from profile snapshots away from rendering. */
internal class AddressBarSuggestionIndex private constructor(
    private val privateMode: Boolean,
    private val normal: PrefixIndex,
    private val privateTabs: PrefixIndex,
) {
    fun suggestions(input: String): List<AddressBarPageSuggestion> =
        (if (privateMode) privateTabs else normal).suggestions(input)

    companion object {
        fun empty(): AddressBarSuggestionIndex = AddressBarSuggestionIndex(
            privateMode = false,
            normal = PrefixIndex(emptyList()),
            privateTabs = PrefixIndex(emptyList()),
        )

        fun build(
            tabs: List<Tab>,
            history: HistorySnapshot,
            bookmarks: BookmarksSnapshot,
        ): AddressBarSuggestionIndex {
            val normal = CandidateCollector()
            val privateTabs = CandidateCollector()

            tabs.asSequence()
                .filterNot(Tab::isTaffyTab)
                .sortedByDescending(Tab::isSelected)
                .take(MAX_OPEN_TABS)
                .forEachIndexed { order, tab ->
                    val target = if (tab.isPrivate) privateTabs else normal
                    target.add(
                        id = "tab-${tab.id.value}",
                        title = tab.title,
                        address = tab.host,
                        host = tab.host,
                        source = Suggestion.Source.OPEN_TAB,
                        sourceOrder = order,
                    )
                }

            val bookmarkFolders = (bookmarks as? BookmarksSnapshot.Ready)?.folders.orEmpty()
            bookmarkFolders.asSequence()
                .flatMap { it.bookmarks }
                .take(MAX_BOOKMARKS)
                .forEachIndexed { order, bookmark ->
                    normal.add(
                        id = "bookmark-${bookmark.id.value}",
                        title = bookmark.title,
                        address = bookmark.address,
                        host = bookmark.host,
                        source = Suggestion.Source.BOOKMARK,
                        sourceOrder = order,
                    )
                }

            val visits = (history as? HistorySnapshot.Ready)?.visits.orEmpty()
            visits.asSequence()
                .filterNot { it.isPrivate || it.isTaffyWorkingTrail }
                .sortedByDescending(HistoryVisit::visitedAtEpochMillis)
                .take(MAX_HISTORY)
                .forEachIndexed { order, visit ->
                    normal.add(
                        id = "history-${visit.id.value}",
                        title = visit.title,
                        address = visit.address,
                        host = visit.host,
                        source = Suggestion.Source.HISTORY,
                        sourceOrder = order,
                    )
                }

            return AddressBarSuggestionIndex(
                // A transient tab projection with no selected row must fail
                // closed when any private tab exists. Briefly hiding normal
                // suggestions is safe; briefly exposing persistent profile
                // data in a private box is not.
                privateMode = tabs.firstOrNull(Tab::isSelected)?.isPrivate
                    ?: tabs.any(Tab::isPrivate),
                normal = PrefixIndex(normal.values()),
                privateTabs = PrefixIndex(privateTabs.values()),
            )
        }

        private const val MAX_OPEN_TABS = 48
        private const val MAX_BOOKMARKS = 96
        private const val MAX_HISTORY = 96
    }

    private class CandidateCollector {
        private val candidates = linkedMapOf<String, Candidate>()

        fun add(
            id: String,
            title: String,
            address: String,
            host: String,
            source: Suggestion.Source,
            sourceOrder: Int,
        ) {
            val candidate = Candidate.create(id, title, address, host, source, sourceOrder)
                ?: return
            candidates.putIfAbsent(candidate.destinationKey, candidate)
        }

        fun values(): List<Candidate> = candidates.values.toList()
    }

    private class PrefixIndex(candidates: List<Candidate>) {
        private val root = MutablePrefixNode().apply {
            candidates.forEach { candidate ->
                candidate.searchTerms().distinct().forEach { term -> insert(term, candidate) }
            }
        }.freeze(prefix = "")

        fun suggestions(input: String): List<AddressBarPageSuggestion> {
            val query = input.trim().lowercase()
            if (query.length !in MIN_QUERY_CHARS..MAX_INDEXED_TERM_CHARS) return emptyList()
            var node = root
            for (character in query) {
                node = node.children[character] ?: return emptyList()
            }
            return node.suggestions
        }
    }

    /** Build-only trie; every rendering-thread lookup uses the frozen form below. */
    private class MutablePrefixNode {
        private val children = mutableMapOf<Char, MutablePrefixNode>()
        private val candidates = linkedSetOf<Candidate>()

        fun insert(term: String, candidate: Candidate) {
            var node = this
            term.take(MAX_INDEXED_TERM_CHARS).forEachIndexed { index, character ->
                node = node.children.getOrPut(character, ::MutablePrefixNode)
                if (index + 1 >= MIN_QUERY_CHARS) node.candidates += candidate
            }
        }

        fun freeze(prefix: String): PrefixNode = PrefixNode(
            children = children.mapValues { (character, child) ->
                child.freeze(prefix + character)
            },
            suggestions = candidates.asSequence()
                .mapNotNull { it.match(prefix) }
                .sortedWith(
                    compareBy<Match>(Match::score)
                        .thenBy { SOURCE_PRIORITY.getValue(it.candidate.source) }
                        .thenBy { it.candidate.sourceOrder }
                        .thenBy { it.candidate.id },
                )
                .take(AddressBarSuggestionSource.MAX_RESULTS)
                .map(Match::toSuggestion)
                .toList(),
        )
    }

    private data class PrefixNode(
        val children: Map<Char, PrefixNode>,
        val suggestions: List<AddressBarPageSuggestion>,
    )

    private data class Candidate(
        val id: String,
        val title: String,
        val address: String,
        val host: String,
        val source: Suggestion.Source,
        val sourceOrder: Int,
        val destinationKey: String,
        val normalizedTitle: String,
        val normalizedHost: String,
        val titleWords: List<String>,
        val hostWords: List<String>,
    ) {
        fun searchTerms(): Sequence<String> = sequenceOf(normalizedHost, normalizedTitle) +
            titleWords.asSequence() + hostWords.asSequence()

        fun match(query: String): Match? {
            val score = when {
                normalizedHost == query -> 0
                normalizedTitle == query -> 1
                normalizedHost.startsWith(query) -> 2
                normalizedTitle.startsWith(query) -> 3
                titleWords.any { it.startsWith(query) } -> 4
                hostWords.any { it.startsWith(query) } -> 5
                else -> return null
            }
            return Match(this, score)
        }

        companion object {
            fun create(
                id: String,
                title: String,
                address: String,
                host: String,
                source: Suggestion.Source,
                sourceOrder: Int,
            ): Candidate? {
                if (source !in PAGE_SOURCES) return null
                val safeHost = safeHost(host) ?: return null
                val safeAddress = safeAddress(address, safeHost) ?: return null
                val safeTitle = title.trim().ifEmpty { safeHost }.take(MAX_TITLE_CHARS)
                return Candidate(
                    id = id,
                    title = safeTitle,
                    address = safeAddress,
                    host = safeHost,
                    source = source,
                    sourceOrder = sourceOrder,
                    destinationKey = destinationKey(safeAddress, safeHost),
                    normalizedTitle = safeTitle.lowercase(),
                    normalizedHost = safeHost.lowercase(),
                    titleWords = words(safeTitle),
                    hostWords = words(safeHost),
                )
            }
        }
    }

    private data class Match(val candidate: Candidate, val score: Int) {
        fun toSuggestion() = AddressBarPageSuggestion(
            id = candidate.id,
            title = candidate.title,
            address = candidate.address,
            host = candidate.host,
            source = candidate.source,
        )
    }

}

private val PAGE_SOURCES = setOf(
    Suggestion.Source.OPEN_TAB,
    Suggestion.Source.BOOKMARK,
    Suggestion.Source.HISTORY,
)
private val SOURCE_PRIORITY = mapOf(
    Suggestion.Source.OPEN_TAB to 0,
    Suggestion.Source.BOOKMARK to 1,
    Suggestion.Source.HISTORY to 2,
)
private const val MIN_QUERY_CHARS = 2
private const val MAX_INDEXED_TERM_CHARS = 64
private const val MAX_TITLE_CHARS = 256
private const val MAX_ADDRESS_CHARS = 8_192
private const val MAX_WORDS = 6

private fun words(value: String): List<String> {
    val normalized = value.lowercase()
    val result = ArrayList<String>(MAX_WORDS)
    var wordStart = -1
    for (index in 0..normalized.length) {
        val inWord = index < normalized.length && normalized[index].isLetterOrDigit()
        if (inWord && wordStart < 0) wordStart = index
        if (!inWord && wordStart >= 0) {
            if (index - wordStart >= MIN_QUERY_CHARS) {
                val word = normalized.substring(wordStart, index)
                if (word !in result) result += word
                if (result.size == MAX_WORDS) return result
            }
            wordStart = -1
        }
    }
    return result
}

private fun safeHost(value: String): String? = value.trim().lowercase().takeIf { host ->
    host.isNotEmpty() &&
        host.length <= 253 &&
        host.none(Char::isWhitespace) &&
        host.none { it in "/?#@" }
}

private fun safeAddress(value: String, host: String): String? {
    val address = value.trim()
    if (address.isEmpty() || address.length > MAX_ADDRESS_CHARS) return null
    if (address.equals(host, ignoreCase = true)) return address
    val uri = runCatching { URI(address) }.getOrNull() ?: return null
    val scheme = uri.scheme?.lowercase()
    if (scheme != "http" && scheme != "https") return null
    if (uri.rawUserInfo != null || uri.host.isNullOrBlank()) return null
    return address
}

private fun destinationKey(address: String, host: String): String {
    val uri = runCatching { URI(address) }.getOrNull()
    val path = uri?.path.orEmpty()
    val isRoot = uri != null &&
        (path.isEmpty() || path == "/") &&
        uri.rawQuery == null &&
        uri.rawFragment == null
    return if (address.equals(host, ignoreCase = true) || isRoot) {
        "root:${host.lowercase()}"
    } else {
        "page:$address"
    }
}
