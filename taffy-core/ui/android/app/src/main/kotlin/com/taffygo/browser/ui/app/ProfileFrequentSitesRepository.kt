// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.Clock
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext

/**
 * Frequent sites persisted in the profile's own PrefService.
 *
 * The encoding is the registry pattern `AndroidProfileSecureMaterialStore`
 * uses: one line per record, tab-separated fields, one preference value as
 * the commit unit. Hosts never contain whitespace, so the host field needs no
 * escaping; the title is the one free-text field and is scrubbed of the two
 * separators before it is written.
 *
 * Every store access runs on the main dispatcher because the PrefService
 * behind [ProfilePreferenceStore] is affine to the thread that owns the
 * profile. The recorder calls in from a background scope, so the hop is made
 * here rather than trusted to every caller — and one thread doing every
 * read-modify-write is also what keeps two rapid visits from losing a count.
 *
 * A line that does not parse is dropped rather than kept or thrown over:
 * this is a convenience ranking, and the honest recovery from a corrupt
 * record is a ranking without it.
 */
class ProfileFrequentSitesRepository(
    private val store: ProfilePreferenceStore,
    private val dispatchers: AppDispatchers,
    private val clock: Clock,
) : FrequentSitesRepository {

    // Read once at construction, on the profile's owning thread, exactly as
    // ProfileUserPreferencesRepository reads its values.
    private val current = MutableStateFlow(read())

    override val sites: StateFlow<List<FrequentSite>> = current.asStateFlow()

    override suspend fun recordVisit(host: String, title: String) {
        // A host with whitespace in it is not a host; refusing it here is
        // what keeps the line-based encoding parseable forever.
        if (host.isBlank() || host.any(Char::isWhitespace)) return
        withContext(dispatchers.main) {
            val counted = linkedMapOf<String, FrequentSite>()
            current.value.forEach { site -> counted[site.host] = site }
            val existing = counted[host]
            counted[host] = FrequentSite(
                host = host,
                title = sanitizedTitle(title, fallback = existing?.title.orEmpty()),
                visitCount = (existing?.visitCount ?: 0L) + 1L,
                lastVisitEpochMillis = clock.nowEpochMillis(),
            )
            val ranked = counted.values.sortedWith(RANKING).take(MAX_SITES)
            store.putString(ProfilePreferenceNames.FREQUENT_SITES, encode(ranked))
            current.value = ranked
        }
    }

    private fun read(): List<FrequentSite> =
        store.getString(ProfilePreferenceNames.FREQUENT_SITES)
            .lineSequence()
            .mapNotNull(::decode)
            .sortedWith(RANKING)
            .take(MAX_SITES)
            .toList()

    private fun encode(sites: List<FrequentSite>): String =
        sites.joinToString("\n") { site ->
            listOf(
                site.host,
                site.visitCount.toString(),
                site.lastVisitEpochMillis.toString(),
                site.title,
            ).joinToString(FIELD_SEPARATOR.toString())
        }

    private fun decode(line: String): FrequentSite? {
        val fields = line.split(FIELD_SEPARATOR, limit = 4)
        if (fields.size != 4) return null
        val host = fields.first()
        if (host.isBlank() || host.any(Char::isWhitespace)) return null
        val count = fields[1].toLongOrNull() ?: return null
        val lastVisit = fields[2].toLongOrNull() ?: return null
        if (count <= 0 || lastVisit < 0) return null
        return FrequentSite(
            host = host,
            title = fields.last().take(MAX_TITLE_CHARACTERS),
            visitCount = count,
            lastVisitEpochMillis = lastVisit,
        )
    }

    private fun sanitizedTitle(title: String, fallback: String): String {
        val cleaned = title
            .replace(FIELD_SEPARATOR, ' ')
            .replace('\n', ' ')
            .replace('\r', ' ')
            .trim()
            .take(MAX_TITLE_CHARACTERS)
        return cleaned.ifEmpty { fallback }
    }

    private companion object {
        /** The registry separator the secure-material store also uses. */
        const val FIELD_SEPARATOR = '\t'

        /** How many hosts the store keeps; the ranking prunes past it. */
        const val MAX_SITES = 50

        /** A title is a label, not a document. */
        const val MAX_TITLE_CHARACTERS = 200

        /** Most visited first; recency breaks ties; the host keeps it stable. */
        val RANKING: Comparator<FrequentSite> =
            compareByDescending(FrequentSite::visitCount)
                .thenByDescending(FrequentSite::lastVisitEpochMillis)
                .thenBy(FrequentSite::host)
    }
}
