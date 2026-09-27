// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import java.text.Collator
import java.util.Locale

/**
 * The complete country chooser, in the shared bottom-sheet frame.
 *
 * Lives in `:core:ui` so SCR-407 and SCR-006 share it. Features never depend
 * on features. Copy is passed in so each screen owns its words.
 */
@Composable
fun RegionPicker(
    selectedRegionCode: String,
    query: String,
    locale: Locale,
    title: String,
    body: String,
    searchPlaceholder: String,
    onQueryChange: (String) -> Unit,
    onChoose: (String) -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    testTag: String = REGION_PICKER_TEST_TAG,
    searchTestTag: String = REGION_PICKER_SEARCH_TEST_TAG,
    countryTestTagPrefix: String = REGION_COUNTRY_TEST_TAG_PREFIX,
) {
    val countries = remember(locale) { regionOptions(locale) }
    val filtered = remember(countries, query) {
        countries.filter { option ->
            query.isBlank() ||
                option.name.contains(query, ignoreCase = true) ||
                option.code.contains(query, ignoreCase = true)
        }
    }
    TaffyBottomSheet(
        title = title,
        onDismissRequest = onDismiss,
        modifier = modifier,
        testTag = testTag,
    ) {
        Text(
            text = body,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffySearchField(
            value = query,
            onValueChange = onQueryChange,
            placeholder = searchPlaceholder,
            testTag = searchTestTag,
        )
        LazyColumn(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(max = RegionListMaximumHeight),
        ) {
            items(filtered, key = RegionOption::code) { country ->
                RegionRow(
                    country = country,
                    chosen = country.code == selectedRegionCode,
                    testTag = "$countryTestTagPrefix${country.code}",
                    onClick = { onChoose(country.code) },
                )
                if (country != filtered.lastOrNull()) {
                    HorizontalDivider(color = TaffyTheme.colors.outline)
                }
            }
        }
    }
}

/** One country, with a stable code for recognition across translated names. */
@Composable
private fun RegionRow(
    country: RegionOption,
    chosen: Boolean,
    testTag: String,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .heightIn(min = CountryMinimumHeight)
            .testTag(testTag)
            .semantics(mergeDescendants = true) { selected = chosen },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CountryFlagSticker(
            code = country.code,
            width = CountryFlagWidth,
            selected = chosen,
        )
        Text(
            text = country.name,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.weight(1f),
        )
        Text(
            text = country.code,
            style = TaffyTheme.typography.caption,
            color = TaffyTheme.colors.textSecondary,
        )
        if (chosen) {
            Icon(
                imageVector = TaffyIcon.CheckCircle,
                contentDescription = null,
                tint = TaffyTheme.colors.positiveText,
                modifier = Modifier.size(ChosenIconSize),
            )
        }
    }
}

/** The complete platform country list, localized and alphabetized for reading. */
private fun regionOptions(locale: Locale): List<RegionOption> {
    val collator = Collator.getInstance(locale)
    return Locale.getISOCountries()
        .map { code ->
            RegionOption(code, LanguageRegionPolicy.regionDisplayName(code, locale))
        }
        .filter { it.name.isNotBlank() }
        .sortedWith { left, right -> collator.compare(left.name, right.name) }
}

private data class RegionOption(
    val code: String,
    val name: String,
)

const val REGION_PICKER_TEST_TAG: String = "language_region_picker"
const val REGION_PICKER_SEARCH_TEST_TAG: String = "language_region_picker_search"
const val REGION_COUNTRY_TEST_TAG_PREFIX: String = "language_region_country_"

private val CountryMinimumHeight = 48.dp
private val ChosenIconSize = 20.dp
private val CountryFlagWidth = 36.dp
private val RegionListMaximumHeight = 480.dp
