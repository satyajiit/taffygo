// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import java.util.Locale

/**
 * One address-bar search engine: identity, the query address, and who sees it.
 *
 * [onlyInRegions] and [hiddenInRegions] are ISO 3166-1 alpha-2. Null
 * [onlyInRegions] means every country except those in [hiddenInRegions]. A
 * selected engine the current country would hide is still listed, so the
 * person can see what the address bar will use.
 *
 * [englishQuery] is the engine's own parameter for "answer in English",
 * already URL-encoded and without a leading separator. It is appended only to
 * the address Taffy opens for a task, never to a person's own search; the
 * reason is in [SearchEngineCatalog.taskSearchUrl]. An engine with no such
 * parameter leaves it empty and its task address is its ordinary one.
 */
data class SearchEngine(
    val id: SearchEngineId,
    val searchUrlTemplate: String,
    val onlyInRegions: Set<String>? = null,
    val hiddenInRegions: Set<String>? = null,
    val englishQuery: String = "",
) {
    /** Whether this row is offered for [regionCode]. */
    fun offeredIn(regionCode: String): Boolean {
        val region = regionCode.uppercase(Locale.ROOT)
        if (onlyInRegions != null && region !in onlyInRegions) return false
        if (hiddenInRegions != null && region in hiddenInRegions) return false
        return true
    }
}
