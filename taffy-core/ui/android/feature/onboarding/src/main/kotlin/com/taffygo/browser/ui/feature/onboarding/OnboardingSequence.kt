// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination

/**
 * How the first-run body uses the space between the system status bar and the
 * pinned footer.
 */
internal enum class OnboardingBody {
    /** Welcome and similar pages: a scrolling stack from the top. */
    SCROLL,

    /** Showcase pages: children may take the leftover height. */
    FILL,

    /** Get started: the stack sits in the vertical middle, and still scrolls. */
    CENTER,
}

/**
 * The frame the first-run screens share: an optional header pinned above a
 * body, and an optional footer pinned under it.
 *
 * The header is pinned rather than scrolled because what sits there is a pair
 * of destination controls — appearance and language — and a control that leaves
 * the screen when the hero is read is a control nobody finds. The body still
 * scrolls, which is what keeps parity row PAR-A11Y-003 honest at 200% text.
 *
 * The sequence does not use `TaffyScreen` because it has no title bar or back
 * arrow. It keeps the system status bar clear without adding a stepper or a
 * page-category chip beneath it.
 */
@Composable
internal fun OnboardingSequence(
    destination: TaffyDestination,
    modifier: Modifier = Modifier,
    body: OnboardingBody = OnboardingBody.SCROLL,
    showBackdrop: Boolean = true,
    header: (@Composable RowScope.() -> Unit)? = null,
    footer: (@Composable ColumnScope.() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(TaffyTheme.colors.surface)
            .testTag(destination.screenId),
    ) {
        if (showBackdrop) {
            OnboardingBackdrop(modifier = Modifier.fillMaxSize())
        }
        Column(
            modifier = Modifier
                .fillMaxSize()
                .windowInsetsPadding(TaffyEdges.sides),
        ) {
            Spacer(
                modifier = Modifier
                    .fillMaxWidth()
                    .windowInsetsPadding(TaffyEdges.top),
            )
            if (header != null) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(
                            start = TaffyTheme.spacing.screenMargin,
                            top = TaffyTheme.spacing.tight,
                            end = TaffyTheme.spacing.screenMargin,
                        ),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                    content = header,
                )
            }
            OnboardingBodySlot(body = body, content = content)
            if (footer != null) {
                Column(
                    modifier = Modifier
                        .windowInsetsPadding(TaffyEdges.bottom)
                        .padding(
                            start = TaffyTheme.spacing.screenMargin,
                            top = TaffyTheme.spacing.snug,
                            end = TaffyTheme.spacing.screenMargin,
                            bottom = TaffyTheme.spacing.tight,
                        ),
                    verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                    content = footer,
                )
            }
        }
    }
}

@Composable
private fun ColumnScope.OnboardingBodySlot(
    body: OnboardingBody,
    content: @Composable ColumnScope.() -> Unit,
) {
    val padding = Modifier.padding(
        start = TaffyTheme.spacing.screenMargin,
        top = TaffyTheme.spacing.snug,
        end = TaffyTheme.spacing.screenMargin,
        bottom = TaffyTheme.spacing.tight,
    )
    when (body) {
        OnboardingBody.SCROLL -> Column(
            modifier = Modifier
                .weight(1f)
                .verticalScroll(rememberScrollState())
                .then(padding),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            content = content,
        )
        OnboardingBody.FILL -> Column(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .then(padding),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            content = content,
        )
        OnboardingBody.CENTER -> Box(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .then(padding),
            contentAlignment = Alignment.Center,
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                horizontalAlignment = Alignment.CenterHorizontally,
                content = content,
            )
        }
    }
}

/**
 * Accent as ink: the deep step on paper, the light one on the dark ground.
 *
 * The accent itself is a fill colour and fails contrast as text on either
 * ground, so no screen writes words in it.
 */
@Composable
internal fun accentInk(): Color =
    if (TaffyTheme.isDark) TaffyTheme.colors.accentText else TaffyTheme.colors.accentDeep
