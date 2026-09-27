// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.SearchEngineId

internal fun searchEngineNameRes(id: SearchEngineId): Int = when (id) {
    SearchEngineId.GOOGLE -> R.string.taffy_search_engine_google
    SearchEngineId.BING -> R.string.taffy_search_engine_bing
    SearchEngineId.DUCKDUCKGO -> R.string.taffy_search_engine_duckduckgo
    SearchEngineId.DUCKDUCKGO_DE -> R.string.taffy_search_engine_duckduckgo_de
    SearchEngineId.DUCKDUCKGO_AU_NZ_IE -> R.string.taffy_search_engine_duckduckgo_au_nz_ie
    SearchEngineId.YAHOO -> R.string.taffy_search_engine_yahoo
    SearchEngineId.YAHOO_JP -> R.string.taffy_search_engine_yahoo_jp
    SearchEngineId.QWANT -> R.string.taffy_search_engine_qwant
    SearchEngineId.STARTPAGE -> R.string.taffy_search_engine_startpage
    SearchEngineId.ECOSIA -> R.string.taffy_search_engine_ecosia
    SearchEngineId.YANDEX -> R.string.taffy_search_engine_yandex
    SearchEngineId.BRAVE_SEARCH -> R.string.taffy_search_engine_brave_search
}
