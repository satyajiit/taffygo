// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import java.net.URLEncoder
import java.nio.charset.StandardCharsets

/**
 * Brave's prepopulated engines, with Brave affiliate tags stripped.
 *
 * URLs come from `_refs/brave-core/components/search_engines/
 * brave_prepopulated_engines.cc`. Regional DuckDuckGo rows replace the
 * generic DuckDuckGo row in Germany and in Australia, New Zealand and
 * Ireland; Brave's `t=` tags are stripped, so the address is still
 * DuckDuckGo. TOR, Naver and Daum are out. Yahoo! JAPAN is offered only
 * for Japan.
 */
object SearchEngineCatalog {

    private const val SEARCH_TERMS = "{searchTerms}"
    private const val DUCKDUCKGO_SEARCH = "https://duckduckgo.com/?q={searchTerms}"

    // Each engine's own way of being asked for English, already encoded and
    // without a leading separator. Google reads `hl` as the interface language
    // and `lr` as the language of the results; Bing reads `setlang` and `mkt`;
    // DuckDuckGo and Startpage read a single region-and-language key; Yahoo!
    // and Ecosia read a market. Qwant, Yandex and Brave Search have no
    // parameter this build is willing to name, so they get none and their task
    // address is their ordinary one.
    private const val GOOGLE_ENGLISH = "hl=en&lr=lang_en"
    private const val BING_ENGLISH = "setlang=en&mkt=en-US"
    private const val DUCKDUCKGO_ENGLISH = "kl=us-en"
    private const val YAHOO_ENGLISH = "vl=lang_en"
    private const val STARTPAGE_ENGLISH = "language=english"
    private const val ECOSIA_ENGLISH = "hl=en"
    private const val JAPAN = "JP"
    private val DUCKDUCKGO_DE_REGIONS = setOf("DE")
    private val DUCKDUCKGO_AU_NZ_IE_REGIONS = setOf("AU", "NZ", "IE")
    private val DUCKDUCKGO_REGIONAL_REGIONS =
        DUCKDUCKGO_DE_REGIONS + DUCKDUCKGO_AU_NZ_IE_REGIONS

    /** Every engine this build knows, in display order. */
    val entries: List<SearchEngine> = listOf(
        SearchEngine(
            SearchEngineId.GOOGLE,
            "https://www.google.com/search?q={searchTerms}",
            englishQuery = GOOGLE_ENGLISH,
        ),
        SearchEngine(
            SearchEngineId.BING,
            "https://www.bing.com/search?q={searchTerms}",
            englishQuery = BING_ENGLISH,
        ),
        SearchEngine(
            SearchEngineId.DUCKDUCKGO,
            DUCKDUCKGO_SEARCH,
            hiddenInRegions = DUCKDUCKGO_REGIONAL_REGIONS,
            englishQuery = DUCKDUCKGO_ENGLISH,
        ),
        SearchEngine(
            SearchEngineId.DUCKDUCKGO_DE,
            DUCKDUCKGO_SEARCH,
            onlyInRegions = DUCKDUCKGO_DE_REGIONS,
            englishQuery = DUCKDUCKGO_ENGLISH,
        ),
        SearchEngine(
            SearchEngineId.DUCKDUCKGO_AU_NZ_IE,
            DUCKDUCKGO_SEARCH,
            onlyInRegions = DUCKDUCKGO_AU_NZ_IE_REGIONS,
            englishQuery = DUCKDUCKGO_ENGLISH,
        ),
        SearchEngine(
            SearchEngineId.YAHOO,
            "https://search.yahoo.com/search?p={searchTerms}",
            englishQuery = YAHOO_ENGLISH,
        ),
        SearchEngine(
            SearchEngineId.YAHOO_JP,
            "https://search.yahoo.co.jp/search?p={searchTerms}",
            onlyInRegions = setOf(JAPAN),
        ),
        SearchEngine(
            SearchEngineId.QWANT,
            "https://www.qwant.com/?q={searchTerms}",
        ),
        SearchEngine(
            SearchEngineId.STARTPAGE,
            "https://www.startpage.com/do/search?q={searchTerms}",
            englishQuery = STARTPAGE_ENGLISH,
        ),
        SearchEngine(
            SearchEngineId.ECOSIA,
            "https://www.ecosia.org/search?q={searchTerms}",
            englishQuery = ECOSIA_ENGLISH,
        ),
        SearchEngine(
            SearchEngineId.YANDEX,
            "https://yandex.com/search/?text={searchTerms}",
        ),
        SearchEngine(
            SearchEngineId.BRAVE_SEARCH,
            "https://search.brave.com/search?q={searchTerms}",
        ),
    )

    /** Engines offered for [regionCode], plus [selected] when it would be hidden. */
    fun listed(regionCode: String, selected: SearchEngineId): List<SearchEngine> {
        val visible = entries.filter { it.offeredIn(regionCode) }
        if (visible.any { it.id == selected }) return visible
        val extra = entries.firstOrNull { it.id == selected } ?: return visible
        return visible + extra
    }

    /** The catalog row for [id], or the default Google row. */
    fun byId(id: SearchEngineId): SearchEngine =
        entries.firstOrNull { it.id == id } ?: entries.first()

    /**
     * The address the engine opens for [query], or null when there is nothing
     * to send. The template placeholder is filled once; a blank query is not
     * an address.
     */
    fun searchUrl(id: SearchEngineId, query: String): String? {
        val trimmed = query.trim()
        if (trimmed.isEmpty()) return null
        val encoded = URLEncoder.encode(trimmed, StandardCharsets.UTF_8.name())
        return byId(id).searchUrlTemplate.replace(SEARCH_TERMS, encoded)
    }

    /**
     * The address Taffy opens for a task's [query] on the engine [id].
     *
     * The same address a person's own search would open, plus the engine's own
     * parameter for English results. A person's search is left alone: it is
     * their search, in their language, and nothing downstream reads it. A
     * task's search is read by a model whose instructions and whose goal are
     * in English, and it becomes the task's evidence — so a results page
     * served in the device's regional language is evidence the answer cannot
     * quote and links the task cannot recognise. On a phone in India, Google
     * answered a task in Hindi and the errand read a page it could not use
     * (decision 0173).
     *
     * An engine with no such parameter gets its ordinary address rather than a
     * guessed one.
     */
    fun taskSearchUrl(id: SearchEngineId, query: String): String? {
        val address = searchUrl(id, query) ?: return null
        val english = byId(id).englishQuery
        if (english.isEmpty()) return address
        return address + (if (address.contains('?')) "&" else "?") + english
    }
}
