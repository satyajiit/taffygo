// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.material3.Text
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.taffyString

/** Theme preview cards. Chosen is an accent ring, not a radio. */
@Composable
internal fun AppearanceThemeSection(
    state: AppearanceUiState,
    onIntent: (AppearanceIntent) -> Unit,
) {
    SettingsHomeEyebrow(title = taffyString(R.string.taffy_appearance_theme))
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .testTag(THEME_LIST_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        ThemePreference.entries.forEach { theme ->
            AppearanceThemeCard(
                theme = theme,
                chosen = theme == state.theme,
                onClick = { onIntent(AppearanceIntent.ChooseTheme(theme)) },
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun AppearanceThemeCard(
    theme: ThemePreference,
    chosen: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val name = taffyString(themeName(theme))
    val shape = TaffyTheme.shapes.card
    val ring = if (chosen) TaffyTheme.colors.accentDeep else TaffyTheme.colors.outline
    val stroke = if (chosen) TaffyBorders.emphasis else TaffyBorders.standard
    TaffyPressable(
        onClick = onClick,
        modifier = modifier.semantics(mergeDescendants = true) {
            contentDescription = name
            selected = chosen
        },
        testTag = "$THEME_TEST_TAG_PREFIX${theme.label}",
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                .clip(shape)
                .background(TaffyTheme.colors.surfaceRaised)
                .border(stroke, ring, shape)
                .padding(TaffyTheme.spacing.tight),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            ThemePreviewCanvas(theme = theme)
            Text(
                text = name,
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.Center,
            )
        }
    }
}

@Composable
private fun ThemePreviewCanvas(theme: ThemePreference) {
    val shape = TaffyTheme.shapes.tile
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(PreviewHeight)
            .clip(shape)
            .border(TaffyBorders.standard, PreviewFrame, shape),
    ) {
        when (theme) {
            ThemePreference.LIGHT -> ThemeMiniChrome(paper = LightPaper, ink = LightInk, bar = LightBar)
            ThemePreference.DARK -> ThemeMiniChrome(paper = DarkPaper, ink = DarkInk, bar = DarkBar)
            ThemePreference.SYSTEM -> Row(Modifier.fillMaxSize()) {
                Box(Modifier.weight(1f).fillMaxHeight()) {
                    ThemeMiniChrome(paper = LightPaper, ink = LightInk, bar = LightBar)
                }
                Box(Modifier.weight(1f).fillMaxHeight()) {
                    ThemeMiniChrome(paper = DarkPaper, ink = DarkInk, bar = DarkBar)
                }
            }
        }
    }
}

@Composable
private fun ThemeMiniChrome(paper: Color, ink: Color, bar: Color) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(paper)
            .padding(PreviewInset),
        verticalArrangement = Arrangement.spacedBy(PreviewGap),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(PreviewStrip)
                .clip(TaffyTheme.shapes.chip)
                .background(bar),
        )
        Box(
            modifier = Modifier
                .fillMaxWidth(0.72f)
                .height(PreviewStrip)
                .clip(TaffyTheme.shapes.chip)
                .background(ink.copy(alpha = 0.35f)),
        )
        Spacer(Modifier.weight(1f))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(PreviewGap),
        ) {
            repeat(3) {
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .height(PreviewDot)
                        .clip(TaffyTheme.shapes.pill)
                        .background(if (it == 1) AccentMark else bar),
                )
            }
        }
    }
}

internal fun themeName(theme: ThemePreference) = when (theme) {
    ThemePreference.SYSTEM -> R.string.taffy_appearance_theme_system
    ThemePreference.LIGHT -> R.string.taffy_appearance_theme_light
    ThemePreference.DARK -> R.string.taffy_appearance_theme_dark
}

private val PreviewHeight = 88.dp
private val PreviewInset = 6.dp
private val PreviewGap = 4.dp
private val PreviewStrip = 6.dp
private val PreviewDot = 5.dp
private val LightPaper = Color(0xFFFBF8F3)
private val LightInk = Color(0xFF1C1917)
private val LightBar = Color(0xFFE8E0D4)
private val DarkPaper = Color(0xFF0E0D0C)
private val DarkInk = Color(0xFFFBF8F3)
private val DarkBar = Color(0xFF2A2622)
private val AccentMark = Color(0xFFE8AA4E)
private val PreviewFrame = Color(0x33000000)
