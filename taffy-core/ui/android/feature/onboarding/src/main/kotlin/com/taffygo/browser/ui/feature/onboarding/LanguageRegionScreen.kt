// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import com.taffygo.browser.ui.core.ui.CountryFlagSticker
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-006 — Language and region. */
@Composable
fun LanguageRegionScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: LanguageRegionViewModel = screenViewModel(TaffyDestination.LanguageRegion)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    LanguageRegionContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = { navigator.goBack() },
        modifier = modifier,
    )
}

/** The 12B list-card layout, with the action kept in a fixed footer. */
@Composable
fun LanguageRegionContent(
    state: LanguageRegionUiState,
    onIntent: (LanguageRegionIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val locale = LocalConfiguration.current.locales[0]
    // The country policy supplies both the rows and the safe selection. The
    // coercion is defensive: persistence already prevents an invalid pair,
    // but a preview or restored UI state must not display one either.
    val selectedLanguage = LanguageRegionPolicy.coerce(state.appLanguage, state.regionCode)
    val languages = LanguageRegionPolicy.availableLanguages(state.regionCode)
        .filter { language ->
            val name = taffyString(languageName(language))
            val note = taffyString(languageNote(language))
            state.searchQuery.isBlank() ||
                name.contains(state.searchQuery, ignoreCase = true) ||
                note.contains(state.searchQuery, ignoreCase = true)
        }

    TaffyScreen(
        destination = TaffyDestination.LanguageRegion,
        title = taffyString(R.string.taffy_language_region_title),
        onBack = onBack,
        modifier = modifier,
        footer = {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_language_region_done),
                onClick = { onIntent(LanguageRegionIntent.Done) },
                modifier = Modifier.fillMaxWidth(),
                testTag = LANGUAGE_REGION_DONE_TEST_TAG,
            )
        },
    ) {
        LanguageSearchField(
            query = state.searchQuery,
            onQueryChange = { onIntent(LanguageRegionIntent.SearchChanged(it)) },
        )

        LanguageChoiceCard(label = taffyString(R.string.taffy_language_region_region_label)) {
            LanguageChoiceRow(
                title = regionDisplayName(state.regionCode, locale),
                supporting = taffyString(R.string.taffy_language_region_region_note),
                leading = {
                    CountryFlagSticker(
                        code = state.regionCode,
                        width = RegionFlagWidth,
                        selected = true,
                    )
                },
                chosen = true,
                testTag = LANGUAGE_REGION_REGION_TEST_TAG,
            )
            LanguageDivider()
            LanguageChoiceRow(
                title = taffyString(R.string.taffy_language_region_region_all),
                supporting = taffyString(R.string.taffy_language_region_region_all_note),
                icon = TaffyIcon.GlobeSimple,
                chosen = false,
                onClick = { onIntent(LanguageRegionIntent.OpenRegionPicker) },
                testTag = LANGUAGE_REGION_OPEN_PICKER_TEST_TAG,
                trailingIcon = TaffyIcon.CaretRight,
            )
        }

        LanguageChoiceCard(label = taffyString(R.string.taffy_language_region_language_label)) {
            if (languages.isEmpty()) {
                Text(
                    text = taffyString(R.string.taffy_language_region_no_matches),
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.padding(vertical = TaffyTheme.spacing.snug),
                )
            } else {
                languages.forEachIndexed { index, language ->
                    val name = taffyString(languageName(language))
                    LanguageChoiceRow(
                        title = name,
                        supporting = taffyString(languageNote(language)),
                        icon = TaffyIcon.GlobeSimple,
                        chosen = language == selectedLanguage,
                        onClick = { onIntent(LanguageRegionIntent.ChooseLanguage(language)) },
                        testTag = "$LANGUAGE_REGION_LANGUAGE_TEST_TAG_PREFIX${language.label}",
                    )
                    if (index != languages.lastIndex) LanguageDivider()
                }
            }
        }

        Row(
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.Top,
        ) {
            Icon(
                imageVector = TaffyIcon.Info,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(NoteIconSize),
            )
            Text(
                text = taffyString(R.string.taffy_language_region_footnote),
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }

    if (state.regionPickerVisible) {
        RegionPicker(
            selectedRegionCode = state.regionCode,
            query = state.regionSearchQuery,
            locale = locale,
            onQueryChange = { onIntent(LanguageRegionIntent.RegionSearchChanged(it)) },
            onChoose = { onIntent(LanguageRegionIntent.ChooseRegion(it)) },
            onDismiss = { onIntent(LanguageRegionIntent.CloseRegionPicker) },
        )
    }
}

/** One outlined card containing a 12B section rather than separate row cards. */
@Composable
private fun LanguageChoiceCard(
    label: String? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card)
            .padding(horizontal = TaffyTheme.spacing.snug),
    ) {
        if (label != null) {
            Text(
                text = label,
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.padding(top = TaffyTheme.spacing.snug),
            )
        }
        content()
    }
}

/** A selected or available language/region row inside one shared card. */
@Composable
private fun LanguageChoiceRow(
    title: String,
    supporting: String,
    chosen: Boolean,
    // Required parameters, then `modifier`, then the rest. Compose's own
    // convention, and lint enforces it: a caller that passes a modifier
    // positionally to a composable that moved it would compile and lay out
    // wrongly, so the order is part of the API rather than a preference.
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    leading: (@Composable () -> Unit)? = null,
    onClick: (() -> Unit)? = null,
    testTag: String? = null,
    trailingIcon: ImageVector? = null,
) {
    val description = taffyString(R.string.taffy_onboarding_pair, title, supporting)
    Row(
        modifier = modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .heightIn(min = ChoiceMinimumHeight)
            .padding(vertical = TaffyTheme.spacing.tight)
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
            .semantics(mergeDescendants = true) {
                contentDescription = description
                selected = chosen
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (leading != null) {
            leading()
        } else if (icon != null) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(ChoiceIconSize),
            )
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = supporting,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        when {
            trailingIcon != null -> Icon(
                imageVector = trailingIcon,
                contentDescription = null,
                tint = TaffyTheme.colors.textTertiary,
                modifier = Modifier.size(TrailingIconSize),
            )
            chosen -> Icon(
                imageVector = TaffyIcon.CheckCircle,
                contentDescription = null,
                tint = TaffyTheme.colors.textPrimary,
                modifier = Modifier.size(ChosenIconSize),
            )
            else -> Box(
                modifier = Modifier
                    .size(ChosenIconSize)
                    .clip(TaffyTheme.shapes.pill)
                    .border(TaffyBorders.emphasis, TaffyTheme.colors.hairline, TaffyTheme.shapes.pill),
            )
        }
    }
}

@Composable
private fun LanguageDivider() {
    HorizontalDivider(color = TaffyTheme.colors.outline)
}

private fun languageName(language: AppLanguage) = when (language) {
    AppLanguage.SYSTEM -> R.string.taffy_language_region_system
    AppLanguage.ENGLISH -> R.string.taffy_language_region_english
    AppLanguage.HINDI -> R.string.taffy_language_region_hindi
}

private fun languageNote(language: AppLanguage) = when (language) {
    AppLanguage.SYSTEM -> R.string.taffy_language_region_system_note
    AppLanguage.ENGLISH -> R.string.taffy_language_region_english_note
    AppLanguage.HINDI -> R.string.taffy_language_region_hindi_note
}

const val LANGUAGE_REGION_SEARCH_TEST_TAG: String = "language_region_search"
const val LANGUAGE_REGION_REGION_TEST_TAG: String = "language_region_region"
const val LANGUAGE_REGION_OPEN_PICKER_TEST_TAG: String = "language_region_open_picker"
const val LANGUAGE_REGION_LANGUAGE_TEST_TAG_PREFIX: String = "language_region_language_"
const val LANGUAGE_REGION_DONE_TEST_TAG: String = "language_region_done"

private val ChoiceMinimumHeight = 58.dp
private val ChoiceIconSize = 17.dp
private val RegionFlagWidth = 36.dp
private val ChosenIconSize = 20.dp
private val TrailingIconSize = 15.dp
private val NoteIconSize = 13.dp
