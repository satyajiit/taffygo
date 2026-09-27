// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.border
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.ui.TaffyBrandLockup
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-001 — Welcome. */
@Composable
fun WelcomeScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: WelcomeViewModel = screenViewModel(TaffyDestination.OnboardingWelcome)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    WelcomeContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun WelcomeContent(
    state: WelcomeUiState,
    onIntent: (WelcomeIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    OnboardingSequence(
        destination = TaffyDestination.OnboardingWelcome,
        modifier = modifier,
        // Appearance leads, language trails: the pair reads left-to-right as
        // "how this looks" then "what it says", and neither scrolls away.
        header = {
            WelcomeThemeSwitcher(
                theme = state.theme,
                onChoose = { onIntent(WelcomeIntent.ChooseTheme(it)) },
            )
            // At 200% text the pair no longer fits a 360 dp row at their
            // natural sizes. The chip is the half that gives way: it
            // ellipsizes its one word, while the switcher's two touch targets
            // stay whole.
            LanguageChip(
                language = state.appLanguage,
                regionCode = state.regionCode,
                onClick = { onIntent(WelcomeIntent.OpenLanguageRegion) },
                modifier = Modifier.weight(1f, fill = false),
            )
        },
        footer = {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_welcome_start),
                onClick = { onIntent(WelcomeIntent.StartBrowsing) },
                modifier = Modifier.fillMaxWidth(),
                testTag = WELCOME_START_TEST_TAG,
                icon = TaffyIcon.ArrowRight,
                size = TaffyButtonSize.LARGE,
            )
            Text(
                text = taffyString(R.string.taffy_welcome_footnote),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textSecondary,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth(),
            )
        },
    ) {
        // The mock hangs this block off the bottom of the frame. Text scales to
        // twice its size (parity row PAR-A11Y-003), so the block scrolls
        // instead: an anchored hero is a hero that loses its last line.
        Spacer(modifier = Modifier.height(TaffyTheme.spacing.snug))
        TaffyBrandLockup(height = LockupHeight)
        WelcomeBadge()
        Text(
            text = taffyString(R.string.taffy_welcome_headline),
            style = TaffyTheme.typography.display,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.testTag(WELCOME_HEADLINE_TEST_TAG),
        )
        Text(
            text = taffyString(R.string.taffy_welcome_body),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        WelcomePromise()
    }
}

/**
 * Appearance, as the two answers a first screen can honestly offer.
 *
 * `ThemePreference.SYSTEM` stays in the model and is still every fresh
 * profile's default; what this compact pair shows for it is the appearance
 * the system is *resolving to right now* — read from [TaffyTheme.isDark],
 * the one resolved fact every surface on this screen is already drawn from,
 * never `isSystemInDarkTheme()` directly. So the control always answers
 * "which of these two is the screen in", which is the only question its two
 * segments can ask, and it is never in the wrong: a system-following profile
 * on a dark phone shows Dark selected because the screen *is* dark.
 *
 * Tapping a segment pins that appearance. The way back to follow-the-system
 * is the settings Appearance screen, which offers all three values with
 * words beside them — the place for a policy, where this is a light switch.
 */
@Composable
private fun WelcomeThemeSwitcher(
    theme: ThemePreference,
    onChoose: (ThemePreference) -> Unit,
    modifier: Modifier = Modifier,
) {
    val groupLabel = taffyString(R.string.taffy_welcome_theme_label)
    val resolvedDark = when (theme) {
        ThemePreference.LIGHT -> false
        ThemePreference.DARK -> true
        ThemePreference.SYSTEM -> TaffyTheme.isDark
    }
    Row(
        modifier = modifier
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.surfaceSunken)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.pill)
            .padding(ThemeSwitcherPadding)
            .selectableGroup()
            // Two icons with no group name are two unexplained icons. The
            // label names what the pair chooses; `selectableGroup` is what
            // makes them read as one choice rather than two buttons.
            .semantics { contentDescription = groupLabel }
            .testTag(WELCOME_THEME_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(ThemeSwitcherPadding),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ThemeChoiceIcon(
            glyph = TaffyIcon.Sun,
            selected = !resolvedDark,
            description = taffyString(R.string.taffy_welcome_theme_light),
            onClick = { onChoose(ThemePreference.LIGHT) },
            modifier = Modifier.testTag(WELCOME_THEME_LIGHT_TEST_TAG),
        )
        ThemeChoiceIcon(
            glyph = TaffyIcon.MoonStars,
            selected = resolvedDark,
            description = taffyString(R.string.taffy_welcome_theme_dark),
            onClick = { onChoose(ThemePreference.DARK) },
            modifier = Modifier.testTag(WELCOME_THEME_DARK_TEST_TAG),
        )
    }
}

/** One vendored glyph, on a full touch target with a spoken label. */
@Composable
private fun ThemeChoiceIcon(
    glyph: androidx.compose.ui.graphics.vector.ImageVector,
    selected: Boolean,
    description: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val tint = if (selected) TaffyTheme.colors.textPrimary else TaffyTheme.colors.textSecondary
    Box(
        modifier = modifier
            .size(ThemeChoiceSize)
            .clip(TaffyTheme.shapes.pill)
            .background(if (selected) TaffyTheme.colors.surfaceRaised else Color.Transparent)
            .border(
                TaffyBorders.standard,
                if (selected) TaffyTheme.colors.textPrimary else Color.Transparent,
                TaffyTheme.shapes.pill,
            )
            .clickable(onClick = onClick)
            .semantics {
                this.selected = selected
                role = Role.RadioButton
            },
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            imageVector = glyph,
            contentDescription = description,
            tint = tint,
            modifier = Modifier.size(ThemeGlyphSize),
        )
    }
}

/** The outlined mark above the headline: what this application is, in two words. */
@Composable
private fun WelcomeBadge() {
    Row(
        modifier = Modifier
            .clip(TaffyTheme.shapes.pill)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.pill)
            .padding(
                horizontal = TaffyTheme.spacing.snug,
                vertical = TaffyTheme.spacing.tight,
            )
            .testTag(WELCOME_BADGE_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.Sparkle,
            contentDescription = null,
            tint = TaffyTheme.colors.accent,
            modifier = Modifier.size(BadgeIconSize),
        )
        Text(
            text = taffyString(R.string.taffy_welcome_badge),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textPrimary,
        )
    }
}

/** The one line that is a commitment rather than a description. */
@Composable
private fun WelcomePromise() {
    Row(
        modifier = Modifier.testTag(WELCOME_PROMISE_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.EyeSlash,
            contentDescription = null,
            tint = TaffyTheme.colors.positive,
            modifier = Modifier.size(PromiseIconSize),
        )
        Text(
            text = taffyString(R.string.taffy_welcome_promise),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
    }
}

/** The tags screen SCR-001's semantics tests name. */
const val WELCOME_BADGE_TEST_TAG: String = "welcome_badge"
const val WELCOME_HEADLINE_TEST_TAG: String = "welcome_headline"
const val WELCOME_PROMISE_TEST_TAG: String = "welcome_promise"
const val WELCOME_START_TEST_TAG: String = "welcome_start"
const val WELCOME_THEME_TEST_TAG: String = "welcome_theme"
const val WELCOME_THEME_LIGHT_TEST_TAG: String = "welcome_theme_light"
const val WELCOME_THEME_DARK_TEST_TAG: String = "welcome_theme_dark"

// The mock's mark and glyph sizes (handoff screen 10; px read as dp). The
// switcher is compact on purpose: two 32 dp segments, so the pair with the
// language chip reads as one quiet header line rather than a control panel.
private val LockupHeight = 50.dp
private val BadgeIconSize = 12.dp
private val PromiseIconSize = 18.dp
private val ThemeChoiceSize = 32.dp
private val ThemeGlyphSize = 16.dp
private val ThemeSwitcherPadding = 2.dp
