// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.platform.LocalConfiguration
import java.math.BigInteger
import java.text.NumberFormat

/**
 * Render a bare count, the only way the UI layer shows one.
 *
 * A count that is part of a phrase is a plural and goes through [taffyPlural];
 * this is for the count that stands alone — a badge, a detail row. `toString()`
 * always yields Latin digits with no grouping, which is English rather than
 * data: the Hindi locale renders Devanagari digits, and a large count groups
 * the way the locale groups. The locale is read from the composition, so a
 * language change recomposes the number like it recomposes a string.
 *
 * The string transformer does not apply here: the pseudo-localization variant
 * marks text that came from a resource, and a computed number did not.
 */
@Composable
@ReadOnlyComposable
fun taffyCount(count: Int): String = taffyCount(count.toLong())

/** The same rendering for a count that arrives as a 64-bit contract field. */
@Composable
@ReadOnlyComposable
fun taffyCount(count: Long): String {
    val locale = LocalConfiguration.current.locales[0]
    return NumberFormat.getInstance(locale).format(count)
}

/** Exact localized rendering for an unsigned 64-bit contract count. */
@Composable
@ReadOnlyComposable
fun taffyCount(count: ULong): String {
    val locale = LocalConfiguration.current.locales[0]
    return NumberFormat.getInstance(locale).format(BigInteger(count.toString()))
}
