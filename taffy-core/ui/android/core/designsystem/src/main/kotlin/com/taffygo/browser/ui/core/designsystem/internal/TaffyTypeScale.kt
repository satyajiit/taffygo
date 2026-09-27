// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem.internal

import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.taffygo.browser.ui.core.designsystem.R
import com.taffygo.browser.ui.core.designsystem.TaffyTypography

/**
 * The compact semantic scale behind [TaffyTypography].
 *
 * It follows the handoff's semantic hierarchy: primary body copy is 14sp,
 * then detail, caption, and micro each step down by one. Keeping those
 * recipes here prevents a supporting sentence from becoming a screen-local
 * `fontSize` override. Sizes are `sp`, not fixed pixels, so the compact base
 * still follows Android's font-size preference.
 *
 * Space Grotesk is vendored under the SIL Open Font License. The explicit font
 * resources pin the variable font's weight axis before Compose loads it; some
 * Android font stacks otherwise render the variable font at its light default.
 */
internal object TaffyTypeScale {

    /** The one place the type family is named. */
    val PRODUCT_TYPEFACE: FontFamily = FontFamily(
        Font(R.font.taffy_space_grotesk_regular, weight = FontWeight.W400),
        Font(R.font.taffy_space_grotesk_medium, weight = FontWeight.W500),
        Font(R.font.taffy_space_grotesk_semibold, weight = FontWeight.W600),
        Font(R.font.taffy_space_grotesk_bold, weight = FontWeight.W700),
    )

    /** Primary running text never drops below the handoff's 14sp floor. */
    const val MINIMUM_BODY_SIZE_SP = 14

    /** Eleven is reserved for short, non-essential metadata in [TaffyTypography.micro]. */
    const val MINIMUM_TEXT_SIZE_SP = 11

    /** The phone scale. */
    val scale = TaffyTypography(
        display = style(weight = FontWeight.W700, size = 24f, lineHeight = 30f, tracking = -0.72f),
        headline = style(weight = FontWeight.W600, size = 20f, lineHeight = 26f),
        title = style(weight = FontWeight.W700, size = 16f, lineHeight = 21f),
        body = style(weight = FontWeight.W400, size = 14f, lineHeight = 20f),
        detail = style(weight = FontWeight.W400, size = 13f, lineHeight = 18f),
        caption = style(weight = FontWeight.W500, size = 12f, lineHeight = 16f),
        micro = style(weight = FontWeight.W500, size = 11f, lineHeight = 15f),
        label = style(weight = FontWeight.W700, size = 13f, lineHeight = 18f),
        numeric = style(
            weight = FontWeight.W500,
            size = 14f,
            lineHeight = 20f,
            fontFeatureSettings = "tnum",
        ),
    )

    /**
     * The tablet scale steps up headings, as the handoff specifies, while the
     * compact reading hierarchy stays stable. Android font scaling, not window
     * width, is the accessibility control for reading size.
     */
    val tabletScale = TaffyTypography(
        display = style(weight = FontWeight.W700, size = 30f, lineHeight = 38f, tracking = -0.9f),
        headline = style(weight = FontWeight.W600, size = 22f, lineHeight = 28f),
        title = style(weight = FontWeight.W700, size = 17f, lineHeight = 22f),
        body = style(weight = FontWeight.W400, size = 14f, lineHeight = 20f),
        detail = style(weight = FontWeight.W400, size = 13f, lineHeight = 18f),
        caption = style(weight = FontWeight.W500, size = 12f, lineHeight = 16f),
        micro = style(weight = FontWeight.W500, size = 11f, lineHeight = 15f),
        label = style(weight = FontWeight.W700, size = 13f, lineHeight = 18f),
        numeric = style(
            weight = FontWeight.W500,
            size = 14f,
            lineHeight = 20f,
            fontFeatureSettings = "tnum",
        ),
    )

    private fun style(
        weight: FontWeight,
        size: Float,
        lineHeight: Float,
        tracking: Float = 0f,
        fontFeatureSettings: String? = null,
    ) = TextStyle(
        fontFamily = PRODUCT_TYPEFACE,
        fontWeight = weight,
        fontSize = size.sp,
        lineHeight = lineHeight.sp,
        letterSpacing = tracking.sp,
        fontFeatureSettings = fontFeatureSettings,
    )
}
