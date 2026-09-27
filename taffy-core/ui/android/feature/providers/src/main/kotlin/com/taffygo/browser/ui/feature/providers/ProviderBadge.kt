// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.ProviderMark

/**
 * The vendor's own mark in the vendor's own colour, or its monogram.
 *
 * One file, because three surfaces draw this badge — the hub's rows, the
 * provider's own page and the vendor sign-in — and a second copy of the mark
 * lookup would be a second answer to "who is this", with the copy that fell
 * behind drawing the wrong logo.
 *
 * It is not the only id-keyed table the badge reads, though, and saying so
 * would be the second answer wearing a different name. The ground comes from
 * [ProviderMarkStyle.inkFor] and the figure on it from [vendoredMark], and the
 * two can disagree: an id the hue table knows and the mark table does not draws
 * a monogram in `textSecondary` on the white chip that hue asked for, which is
 * `#C5C1BB` on white at 1.79:1 — unreadable, and looking like a rendering fault
 * rather than a missing row. Nothing in the type system pairs them, so
 * `ProviderMarkResolutionTest` asserts that a recorded hue implies a mark. The
 * pair drifted once already, on 2026-08-30, over a row that has since left the
 * catalog.
 *
 * Marks stay compiled because decision 0030 will not carry one without an
 * upstream URL and checksum, which a served catalog cannot supply. So a provider
 * this binary has no mark for — a row a catalog update introduced, a person's
 * own, or a vendor whose permitted use could not be argued — draws its monogram
 * on the product's own ground, which is legible in both themes and claims
 * nothing. Both halves are covered: every provider resolves to a mark or to a
 * monogram, and `ProviderMarkResolutionTest` walks the published catalog to say
 * so.
 */
@Composable
internal fun ProviderBadge(providerId: String, name: String, size: Dp = ProviderBadgeSize) {
    val mark = vendoredMark(providerId)
    val ink = ProviderMarkStyle.inkFor(providerId)
    Box(
        modifier = Modifier
            .size(size)
            .clip(TaffyTheme.shapes.chip)
            .background(
                if (ink == null) TaffyTheme.colors.surfaceSunken else ProviderMarkStyle.chip,
            ),
        contentAlignment = Alignment.Center,
    ) {
        if (mark == null) {
            Text(
                text = providerMonogram(name),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textSecondary,
            )
        } else {
            Icon(
                imageVector = mark,
                contentDescription = null,
                tint = ink ?: TaffyTheme.colors.textPrimary,
                modifier = Modifier.size(size * MarkFraction),
            )
        }
    }
}

/**
 * The compiled mark for one catalog provider id, or `null` for its monogram.
 *
 * Visible to the module rather than private so a test can walk the published
 * catalog's ids through it. A badge that drew nothing would be a blank chip on
 * a row a person is being asked to choose, and nothing about the composable
 * itself says which ids reach which branch.
 */
internal fun vendoredMark(providerId: String): ImageVector? = when (providerId) {
    "anthropic" -> ProviderMark.Anthropic
    "baseten" -> ProviderMark.Baseten
    "cerebras" -> ProviderMark.Cerebras
    "deepseek" -> ProviderMark.DeepSeek
    "fireworks" -> ProviderMark.Fireworks
    // GitHub's Copilot mark. GitHub owns two marks at the upstream source and
    // only this one is vendored: the Invertocat's single permitted use was an
    // account sign-in button, and it left with that screen (decision 0201).
    "github-copilot" -> ProviderMark.GitHubCopilot
    // The Gemini mark, which the provenance record in
    // `core/ui/vendor/provider-marks.txt` permits by name for this row: the
    // key Google AI Studio issues is the key that reaches the Gemini API.
    "google-ai-studio" -> ProviderMark.GoogleGemini
    "groq" -> ProviderMark.Groq
    // Kimi and Moonshot are one company and two marks, so they are two rows
    // with the mark each row's own product carries.
    "kimi-coding" -> ProviderMark.Kimi
    "minimax" -> ProviderMark.MiniMax
    "moonshot" -> ProviderMark.MoonshotAi
    "openai" -> ProviderMark.OpenAi
    "openrouter" -> ProviderMark.OpenRouter
    "together" -> ProviderMark.Together
    "xai" -> ProviderMark.Xai
    // `zai` is deliberately absent. lobe-icons publishes the mark, so the
    // URL-and-checksum half of decision 0030 is available; no owner-published
    // brand or terms document could be found on 2026-08-30 to argue a permitted
    // use from, and a mark carried without that argument is the thing the
    // provenance record exists to prevent. The monogram is the right answer.
    else -> null
}

/**
 * The letter a provider with no mark shows instead.
 *
 * Trimmed and raised so the fallback is the same shape for every name, and
 * separate from the composable so the "no provider renders nothing" assertion
 * can reach it.
 */
internal fun providerMonogram(name: String): String = name.trim().take(1).uppercase()

/** A row's badge. A page header asks for a larger one. */
internal val ProviderBadgeSize: Dp = 36.dp

/** How much of the badge the mark itself takes, at any badge size. */
private const val MarkFraction = 0.56f
