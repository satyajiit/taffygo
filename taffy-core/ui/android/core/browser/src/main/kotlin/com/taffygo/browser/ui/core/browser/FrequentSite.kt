// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * One site the person keeps returning to, as the start page's tiles show it.
 *
 * The host is the identity — visits are counted per host, never per full URL,
 * so nothing here can reconstruct a browsing path. The title rides along so a
 * tile can be spoken as a place rather than as a domain, and the two numbers
 * are what the ranking reads: how often, and how recently.
 */
data class FrequentSite(
    /** The host, which is the identity of the record. */
    val host: String,
    /** The page title as the page last gave it. */
    val title: String,
    /** How many committed navigations this host has received. */
    val visitCount: Long,
    /** When the last of them happened, milliseconds since the Unix epoch. */
    val lastVisitEpochMillis: Long,
)
