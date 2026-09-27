// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import java.util.Locale

/**
 * One country's flag, at whatever fidelity is actually available.
 *
 * ## Four by three, and fitted rather than filled
 *
 * The box is 4:3 and the artwork is [ContentScale.Fit] inside it. Flags are not
 * one shape: Nepal's is a pennant and Switzerland's and the Vatican's are
 * square, and a square box would crop the first two while a filled 4:3 box
 * would stretch all three. Letterboxing inside a common box is the only
 * treatment that is correct for every country rather than for most of them.
 *
 * Four by three rather than any other common box because it is the one the
 * corpus draws in — see [FlagAspectRatio].
 *
 * The size is given as a width for the same reason: 4:3 makes the height, so a
 * caller states the one dimension a row actually has to line up.
 *
 * ## The fallback is the platform's flag, then the code
 *
 * With no artwork, this draws the regional-indicator pair the device has a
 * glyph for, and the two-letter ISO code where it does not. That artwork
 * belongs to the device manufacturer, is absent from every Compose preview and
 * every screenshot taken from one, and differs between phones — all of which is
 * why it is the fallback and not the plan.
 */
@Composable
fun CountryFlagImage(
    code: String,
    width: Dp,
    modifier: Modifier = Modifier,
) {
    val state = LocalCountryFlagSource.current.flagFor(code)
    Box(
        modifier = modifier
            .width(width)
            .aspectRatio(FlagAspectRatio),
        contentAlignment = Alignment.Center,
    ) {
        when (state) {
            is CountryFlagState.Ready -> Image(
                bitmap = state.artwork,
                // The row beside it names the country; a flag that also
                // announced it would be heard twice.
                contentDescription = null,
                contentScale = ContentScale.Fit,
                modifier = Modifier.fillMaxSize(),
            )
            // Reserved and empty. See [CountryFlagState.Loading] for why this
            // is not the fallback.
            CountryFlagState.Loading -> Unit
            CountryFlagState.Absent -> Text(
                text = countryFlagLabel(code),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

/** The device's own flag glyph, or the code, with no request and no asset. */
fun countryFlagLabel(code: String): String =
    normalizedFlagCode(code)?.let(::regionalIndicatorFlag)
        ?: code.trim().take(2).uppercase(Locale.ROOT)

/** The code as ISO 3166-1 alpha-2 knows it, or null when it is not one. */
fun normalizedFlagCode(code: String): String? =
    code.trim().uppercase(Locale.ROOT).takeIf(ISO_COUNTRY_CODES::contains)

private fun regionalIndicatorFlag(code: String): String = buildString {
    for (letter in code) {
        appendCodePoint(REGIONAL_INDICATOR_A + (letter - 'A'))
    }
}

private val ISO_COUNTRY_CODES = Locale.getISOCountries().toSet()
private const val REGIONAL_INDICATOR_A = 0x1F1E6

/**
 * Flags are drawn 4:3 and letterboxed; see the file header.
 *
 * Four by three because that is the box the corpus itself draws in. Decision
 * `docs/decisions/0047-the-country-flag-corpus-is-iso-3166-1.md` settles the
 * artwork on flag-icons, which publishes a `4x3` set and a `1x1` set and
 * normalises every flag into whichever is asked for. Three by two was chosen
 * here before the corpus was, and keeping it would have meant letterboxing the
 * pack's own artwork inside a box the pack did not draw — on every flag, not
 * only on Nepal's pennant and Switzerland's square.
 */
private const val FlagAspectRatio = 4f / 3f
