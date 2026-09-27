// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * The address-bar search engines TaffyGo offers.
 *
 * Default is [GOOGLE] (decision 0019). The list is Brave's prepopulated set
 * with affiliate tags stripped: one DuckDuckGo plus the regional DuckDuckGo
 * rows for Germany and for Australia, New Zealand and Ireland, and Yahoo!
 * JAPAN only when the stored country is Japan. TOR, Naver and Daum are out.
 * Which of these may ground a model remains OD-019.
 */
enum class SearchEngineId(
    /** The value written to the store. */
    val wireName: String,
    /** Favicon filename under `vendor/search-engines/`. */
    val markFile: String,
) {
    GOOGLE("google", "google.ico"),
    BING("bing", "bing.ico"),
    DUCKDUCKGO("duckduckgo", "duckduckgo.ico"),
    DUCKDUCKGO_DE("duckduckgo-de", "duckduckgo.ico"),
    DUCKDUCKGO_AU_NZ_IE("duckduckgo-au-nz-ie", "duckduckgo.ico"),
    YAHOO("yahoo", "yahoo.ico"),
    YAHOO_JP("yahoo-jp", "yahoo.ico"),
    QWANT("qwant", "qwant.ico"),
    STARTPAGE("startpage", "startpage.ico"),
    ECOSIA("ecosia", "ecosia.ico"),
    YANDEX("yandex", "yandex.ico"),
    BRAVE_SEARCH("brave-search", "brave-search.ico"),
    ;

    companion object {
        /** The engine a fresh profile uses. */
        val DEFAULT: SearchEngineId = GOOGLE

        /** The stored name, or [DEFAULT] when the value is missing or unknown. */
        fun parse(wireName: String): SearchEngineId =
            entries.firstOrNull { it.wireName == wireName } ?: DEFAULT
    }
}
