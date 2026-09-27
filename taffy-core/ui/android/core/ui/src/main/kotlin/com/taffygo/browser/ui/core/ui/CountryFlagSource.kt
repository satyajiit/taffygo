// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ProvidableCompositionLocal
import androidx.compose.runtime.Stable
import androidx.compose.runtime.staticCompositionLocalOf

/**
 * Where a country's flag artwork comes from.
 *
 * ## Why this is a port and not an implementation
 *
 * There is no image pipeline in this tree: no loader, no cache, no HTTP client
 * on the Android side. The delivery plane fetches the pack in the browser
 * process, and the shell binds that plane over this port.
 *
 * So this is the shape the UI needs, stated by the UI: one country code in, one
 * [CountryFlagState] out, resolved in composition so an implementation may use
 * `produceState` and cancel with the composable that asked.
 *
 * ## The default is the honest answer, not a placeholder
 *
 * [LocalCountryFlagSource] defaults to a source that always answers
 * [CountryFlagState.Absent], so a preview or a host that has not bound the
 * plane draws the fallback and requests nothing. That is the truth in those
 * graphs rather than a stub. The shipping shell overrides it.
 */
@Stable
interface CountryFlagSource {

    /** This country's artwork, or why there is none. */
    @Composable
    fun flagFor(code: String): CountryFlagState
}

/**
 * The source every flag reads, defaulting to one that has no artwork at all.
 *
 * `static`, not `compositionLocalOf`: the source changes once per process at
 * most — when the shell installs the real one — and a static local means a
 * flag does not subscribe to a value that never moves.
 */
val LocalCountryFlagSource: ProvidableCompositionLocal<CountryFlagSource> =
    staticCompositionLocalOf {
        object : CountryFlagSource {
            @Composable
            override fun flagFor(code: String): CountryFlagState = CountryFlagState.Absent
        }
    }
