// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import com.taffygo.browser.ui.core.ui.CountryFlagSticker
import com.taffygo.browser.ui.core.ui.RegionPicker
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.TaffyTileWash
import com.taffygo.browser.ui.core.ui.inkColor
import com.taffygo.browser.ui.core.ui.taffyString

/** Country row, language tiles, and the searchable country sheet. */
@Composable
internal fun AppearanceLanguageSection(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
) {
    val locale = LocalConfiguration.current.locales[0]
    val selectedLanguage = LanguageRegionPolicy.coerce(state.appLanguage, state.regionCode)
    val languages = LanguageRegionPolicy.availableLanguages(state.regionCode)
    val countryName = LanguageRegionPolicy.regionDisplayName(state.regionCode, locale)

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        SettingsHomeEyebrow(title = taffyString(R.string.taffy_appearance_language))
        AppearanceCountryRow(
            regionCode = state.regionCode,
            countryName = countryName,
            onClick = { onIntent(AppearanceIntent.OpenRegionPicker) },
        )
        Text(
            text = taffyString(R.string.taffy_appearance_country_note),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .testTag(LANGUAGE_LIST_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            languages.forEach { language ->
                AppearanceLanguageTile(
                    language = language,
                    chosen = language == selectedLanguage,
                    onClick = { onIntent(AppearanceIntent.ChooseLanguage(language)) },
                )
            }
        }
        Text(
            text = taffyString(R.string.taffy_appearance_language_footnote),
            style = TaffyTheme.typography.caption,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

@Composable
internal fun AppearanceCountryPicker(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
) {
    val locale = LocalConfiguration.current.locales[0]
    RegionPicker(
        selectedRegionCode = state.regionCode,
        query = state.regionSearchQuery,
        locale = locale,
        title = taffyString(R.string.taffy_appearance_country_picker_title),
        body = taffyString(R.string.taffy_appearance_country_picker_body),
        searchPlaceholder = taffyString(R.string.taffy_appearance_country_picker_search),
        onQueryChange = { onIntent(AppearanceIntent.RegionSearchChanged(it)) },
        onChoose = { onIntent(AppearanceIntent.ChooseRegion(it)) },
        onDismiss = { onIntent(AppearanceIntent.CloseRegionPicker) },
        testTag = COUNTRY_PICKER_TEST_TAG,
        searchTestTag = COUNTRY_SEARCH_TEST_TAG,
        countryTestTagPrefix = COUNTRY_TEST_TAG_PREFIX,
    )
}

@Composable
private fun AppearanceCountryRow(
    regionCode: String,
    countryName: String,
    onClick: () -> Unit,
) {
    val spoken = taffyString(
        R.string.taffy_appearance_pair,
        taffyString(R.string.taffy_appearance_country_change),
        countryName,
    )
    TaffyObjectCard(
        onClick = onClick,
        testTag = COUNTRY_PILL_TEST_TAG,
        modifier = Modifier.semantics(mergeDescendants = true) { contentDescription = spoken },
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            CountryFlagSticker(code = regionCode, width = CountryFlagWidth, selected = true)
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = taffyString(R.string.taffy_appearance_country_change),
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                )
                Text(
                    text = countryName,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Icon(
                imageVector = TaffyIcon.CaretRight,
                contentDescription = null,
                tint = TaffyTheme.colors.hairline,
                modifier = Modifier.size(CountryCaretSize),
            )
        }
    }
}

@Composable
private fun AppearanceLanguageTile(
    language: AppLanguage,
    chosen: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val name = taffyString(languageName(language))
    val note = taffyString(languageNote(language))
    val spoken = taffyString(R.string.taffy_appearance_pair, name, note)
    val wash = if (chosen) TaffyTileWash.Accent else TaffyTileWash.Neutral
    val shape = TaffyTheme.shapes.card
    TaffyPressable(
        onClick = onClick,
        modifier = modifier.semantics(mergeDescendants = true) {
            contentDescription = spoken
            selected = chosen
        },
        testTag = "$LANGUAGE_TEST_TAG_PREFIX${language.label}",
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                .clip(shape)
                .background(TaffyTheme.colors.surfaceRaised)
                .border(TaffyBorders.standard, TaffyTheme.colors.outline, shape)
                .padding(TaffyTheme.spacing.cardPadding),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            Icon(
                imageVector = languageGlyph(language),
                contentDescription = null,
                tint = wash.inkColor(),
                modifier = Modifier.size(LanguageGlyphSize),
            )
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                Text(
                    text = name,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = note,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
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
}

private fun languageName(language: AppLanguage) = when (language) {
    AppLanguage.SYSTEM -> R.string.taffy_appearance_language_system
    AppLanguage.ENGLISH -> R.string.taffy_appearance_language_english
    AppLanguage.HINDI -> R.string.taffy_appearance_language_hindi
}

private fun languageNote(language: AppLanguage) = when (language) {
    AppLanguage.SYSTEM -> R.string.taffy_appearance_language_system_note
    AppLanguage.ENGLISH -> R.string.taffy_appearance_language_english_note
    AppLanguage.HINDI -> R.string.taffy_appearance_language_hindi_note
}

private fun languageGlyph(language: AppLanguage): ImageVector = when (language) {
    AppLanguage.SYSTEM -> TaffyIcon.GlobeSimple
    AppLanguage.ENGLISH, AppLanguage.HINDI -> TaffyIcon.Translate
}

private val LanguageGlyphSize = 20.dp
private val ChosenIconSize = 20.dp
private val CountryFlagWidth = 44.dp
private val CountryCaretSize = 18.dp
