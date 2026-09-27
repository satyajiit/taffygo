// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem.internal

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import com.taffygo.browser.ui.core.designsystem.TaffyColors
import com.taffygo.browser.ui.core.designsystem.TaffyTypography

/**
 * Derives a Material 3 color scheme from the Taffy tokens.
 *
 * The UI layer does not theme with Material, but M3 components (`Button`,
 * `Switch`, `OutlinedTextField`) read `MaterialTheme.colorScheme` directly,
 * and without a bridge they draw the default M3 purple. Every role is filled
 * from a token so no baseline colour can leak through: primary is ink on
 * paper (light) or paper on ink (dark), per the handoff's neutral-first rule,
 * and the accent family only ever rides the secondary/tertiary roles.
 */
internal fun taffyColorScheme(darkTheme: Boolean, colors: TaffyColors): ColorScheme {
    val base = if (darkTheme) darkColorScheme() else lightColorScheme()
    return base.copy(
        primary = colors.textPrimary,
        onPrimary = colors.surface,
        secondary = colors.accent,
        onSecondary = colors.accentOn,
        secondaryContainer = colors.accentWash,
        onSecondaryContainer = if (darkTheme) colors.accentText else colors.accentDeep,
        tertiary = if (darkTheme) colors.accentText else colors.accentDeep,
        onTertiary = colors.surface,
        background = colors.surface,
        onBackground = colors.textPrimary,
        surface = colors.surface,
        onSurface = colors.textPrimary,
        surfaceVariant = colors.surfaceRaised,
        onSurfaceVariant = colors.textSecondary,
        surfaceContainerLowest = colors.surfaceRaised,
        surfaceContainerLow = colors.surface,
        surfaceContainer = colors.surface,
        surfaceContainerHigh = colors.surfaceSunken,
        surfaceContainerHighest = colors.surfaceSunken,
        outline = colors.outline,
        outlineVariant = colors.hairline,
        error = colors.danger,
        onError = if (darkTheme) colors.textPrimary else colors.surfaceRaised,
        errorContainer = colors.dangerWash,
        onErrorContainer = colors.dangerText,
    )
}

/**
 * Derives Material 3's roles from the semantic Taffy scale, filling every slot
 * so no component falls back to the platform default family. The `numeric`
 * role has no Material counterpart and stays Taffy-only.
 */
internal fun taffyMaterialTypography(typography: TaffyTypography) = Typography(
    displayLarge = typography.display,
    displayMedium = typography.display,
    displaySmall = typography.display,
    headlineLarge = typography.headline,
    headlineMedium = typography.headline,
    headlineSmall = typography.headline,
    titleLarge = typography.title,
    titleMedium = typography.title,
    titleSmall = typography.label,
    bodyLarge = typography.body,
    bodyMedium = typography.detail,
    bodySmall = typography.caption,
    labelLarge = typography.label,
    labelMedium = typography.caption,
    labelSmall = typography.micro,
)
