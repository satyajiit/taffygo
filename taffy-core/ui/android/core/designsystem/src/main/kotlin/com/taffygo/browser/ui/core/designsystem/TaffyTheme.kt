// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import com.taffygo.browser.ui.core.designsystem.internal.TaffyTypeScale
import com.taffygo.browser.ui.core.designsystem.internal.taffyColorScheme
import com.taffygo.browser.ui.core.designsystem.internal.taffyMaterialTypography

/**
 * The theme every Taffy-owned surface sits inside.
 *
 * Light and dark are both first-class; the caller decides which, because the
 * Appearance setting can override the system (screen SCR-407). Nothing below
 * this point reads the system theme again.
 *
 * The width bucket is measured from the window unless the caller passes one,
 * so a preview and a test stay on the phone tokens until they ask otherwise.
 * Spacing and type follow that bucket: a tablet is not a stretched phone.
 *
 * The tree is also wrapped in a `MaterialTheme` whose scheme and type scale
 * are derived from the same tokens (see `internal/TaffyMaterialBridge.kt`), so
 * M3 components render in Taffy colours instead of the default M3 baseline.
 *
 * A change of theme is not a swap. `TaffyThemeTransition` hands this function a
 * blend of the outgoing and incoming schemes for the length of the change, and
 * that blend is what both the Material bridge and `LocalTaffyColors` are built
 * from — so the whole window travels together rather than cutting behind a wash.
 */
@Composable
fun TaffyTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    windowWidth: TaffyWindowWidth = currentTaffyWindowWidth(),
    reducedMotion: Boolean = currentTaffyReducedMotion(),
    glyphs: TaffyThemeGlyphs? = null,
    content: @Composable () -> Unit,
) {
    val typography = if (windowWidth.usesTabletType) {
        TaffyTypeScale.tabletScale
    } else {
        TaffyTypeScale.scale
    }
    val spacing = TaffySpacing.forWidth(windowWidth)
    // The transition is outside the providers, not inside them, because during a
    // theme change the colours are a blend of the two schemes and every surface
    // below has to be given that blend. Wrapped the other way round it could
    // only draw over a tree that had already changed.
    TaffyThemeTransition(
        darkTheme = darkTheme,
        reducedMotion = reducedMotion,
        glyphs = glyphs,
    ) { colors ->
        MaterialTheme(
            colorScheme = taffyColorScheme(darkTheme, colors),
            typography = taffyMaterialTypography(typography),
        ) {
            CompositionLocalProvider(
                LocalTaffyDarkTheme provides darkTheme,
                LocalTaffyWindowWidth provides windowWidth,
                LocalTaffyColors provides colors,
                LocalTaffyTypography provides typography,
                LocalTaffySpacing provides spacing,
                LocalTaffyShapes provides TaffyShapes.Default,
                LocalTaffyReducedMotion provides reducedMotion,
                content = content,
            )
        }
    }
}

/** The tokens in scope, the way `MaterialTheme` hands its own down. */
object TaffyTheme {

    /** The colour tokens of the theme in scope. */
    val colors: TaffyColors
        @Composable @ReadOnlyComposable get() = LocalTaffyColors.current

    /** The six type roles of the theme in scope. */
    val typography: TaffyTypography
        @Composable @ReadOnlyComposable get() = LocalTaffyTypography.current

    /** The spacing grid of the theme in scope. */
    val spacing: TaffySpacing
        @Composable @ReadOnlyComposable get() = LocalTaffySpacing.current

    /** The corner radii of the theme in scope. */
    val shapes: TaffyShapes
        @Composable @ReadOnlyComposable get() = LocalTaffyShapes.current

    /** The window-width bucket of the theme in scope. */
    val windowWidth: TaffyWindowWidth
        @Composable @ReadOnlyComposable get() = LocalTaffyWindowWidth.current

    /**
     * Whether the theme in scope is the dark one.
     *
     * The Appearance setting can override the system, so a component that draws
     * a light-only or dark-only skin (a border the dark theme drops, a fill the
     * light theme inverts) reads this and never `isSystemInDarkTheme()`.
     */
    val isDark: Boolean
        @Composable @ReadOnlyComposable get() = LocalTaffyDarkTheme.current

    /**
     * Whether the person asked for less motion.
     *
     * Loops hold still and full-window transitions cut over immediately when
     * this is true. Small state changes may still use motion when they remain
     * understandable without tracking it.
     */
    val reducedMotion: Boolean
        @Composable @ReadOnlyComposable get() = LocalTaffyReducedMotion.current

    /** The colour a status tone draws in. Never the whole of a status. */
    @Composable
    @ReadOnlyComposable
    fun toneColor(tone: TaffyStatusTone): androidx.compose.ui.graphics.Color = when (tone) {
        TaffyStatusTone.NEUTRAL -> colors.textSecondary
        TaffyStatusTone.ACCENT -> colors.accent
        TaffyStatusTone.POSITIVE -> colors.positive
        TaffyStatusTone.CAUTION -> colors.caution
        TaffyStatusTone.DANGER -> colors.danger
    }
}

/** Whether motion is suppressed. Provided only by [TaffyTheme]. */
val LocalTaffyReducedMotion = staticCompositionLocalOf<Boolean> {
    error("No reduced-motion flag in scope: wrap the tree in TaffyTheme.")
}

/** Whether the theme in scope is dark. Provided only by [TaffyTheme]. */
val LocalTaffyDarkTheme = staticCompositionLocalOf<Boolean> {
    error("No dark-theme flag in scope: wrap the tree in TaffyTheme.")
}

/** The window-width bucket in scope. Provided only by [TaffyTheme]. */
val LocalTaffyWindowWidth = staticCompositionLocalOf<TaffyWindowWidth> {
    error("No window width in scope: wrap the tree in TaffyTheme.")
}

/** The colour tokens in scope. Provided only by [TaffyTheme]. */
val LocalTaffyColors = staticCompositionLocalOf<TaffyColors> {
    error("No TaffyColors in scope: wrap the tree in TaffyTheme.")
}
/** The type roles in scope. Provided only by [TaffyTheme]. */
val LocalTaffyTypography = staticCompositionLocalOf<TaffyTypography> {
    error("No TaffyTypography in scope: wrap the tree in TaffyTheme.")
}

/** The spacing grid in scope. Provided only by [TaffyTheme]. */
val LocalTaffySpacing = staticCompositionLocalOf<TaffySpacing> {
    error("No TaffySpacing in scope: wrap the tree in TaffyTheme.")
}

/** The corner radii in scope. Provided only by [TaffyTheme]. */
val LocalTaffyShapes = staticCompositionLocalOf<TaffyShapes> {
    error("No TaffyShapes in scope: wrap the tree in TaffyTheme.")
}
