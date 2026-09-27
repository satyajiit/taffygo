// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import android.content.res.Resources
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import com.taffygo.browser.ui.core.ui.CountryFlagSticker
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The chip in the corner of the first-run screens: which language the
 * interface is in, and a way to change it before reading anything else.
 *
 * The chip shows a flag and the language the person is reading. SYSTEM follows
 * the device when that catalogue exists, otherwise English — it is never shown
 * as the implementation phrase "Follow the system". The region is in the flag
 * and in the spoken label, not in the visible words.
 */
@Composable
internal fun LanguageChip(
    language: AppLanguage,
    regionCode: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val locale = LocalConfiguration.current.locales[0]
    val catalogLanguage = LanguageRegionPolicy.resolvedLanguage(
        language,
        deviceLanguageTag(locale.toLanguageTag()),
    )
    val languageName = taffyString(
        when (catalogLanguage) {
            AppLanguage.HINDI -> R.string.taffy_language_chip_hindi
            AppLanguage.ENGLISH, AppLanguage.SYSTEM -> R.string.taffy_language_chip_english
        },
    )
    val name = taffyString(
        R.string.taffy_language_chip_value,
        LanguageRegionPolicy.regionDisplayName(regionCode, locale),
        languageName,
    )
    val described = taffyString(R.string.taffy_language_chip_description, name)
    Box(
        modifier = modifier
            .clickable(onClick = onClick)
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .testTag(LANGUAGE_CHIP_TEST_TAG)
            .semantics(mergeDescendants = true) { contentDescription = described },
        // Centred, not top-aligned. The 44 dp minimum below is the touch
        // target and the pill inside it is 34 dp, so top-aligning left the
        // visible chip riding about seven units above the theme switcher
        // beside it, which is a 44 dp control drawn at its full height.
        contentAlignment = Alignment.Center,
    ) {
        Row(
            modifier = Modifier
                .height(ChipHeight)
                .clip(TaffyTheme.shapes.pill)
                .border(
                    TaffyBorders.standard,
                    TaffyTheme.colors.outline,
                    TaffyTheme.shapes.pill,
                )
                .padding(horizontal = TaffyTheme.spacing.snug),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CountryFlagSticker(code = regionCode, width = FlagWidth)
            Text(
                // The flag already says which region, so the visible label is
                // the language alone. The spoken label keeps both, because the
                // chip changes both and a control that says less than it does
                // is a control that misleads.
                text = languageName,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f, fill = false),
            )
            Icon(
                imageVector = TaffyIcon.CaretDown,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(CaretSize),
            )
        }
    }
}

/** The device's own language, not a per-app pin that may still be in force. */
private fun deviceLanguageTag(fallback: String): String {
    val locales = Resources.getSystem().configuration.locales
    return if (locales.isEmpty) fallback else locales[0].toLanguageTag()
}

/** The chip, for tests. */
const val LANGUAGE_CHIP_TEST_TAG: String = "language_chip"

// The mock's glyph sizes (handoff screen 10; px read as dp).
private val FlagWidth = 24.dp
private val CaretSize = 10.dp
private val ChipHeight = 34.dp
