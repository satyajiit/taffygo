// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.ThinkingLevel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * How much this model thinks before it answers: a row of pills, **Auto** first.
 *
 * Auto is the first pill because it is where everybody starts, and it means the
 * absence of a preference — Taffy deciding. It is not `Off`, which is a person
 * asking for no thinking phase at all, and the two are separate pills carrying
 * separate values so that pressing one can never be mistaken for the other.
 *
 * The row draws nothing when the model offers fewer than two rungs: a radio
 * group with a single option tells a person they have a choice they do not
 * have. The pills after Auto are exactly the model's own catalog entry, so a
 * model that maps no `XHIGH` never shows one.
 */
@Composable
fun ThinkingLevelRow(
    choice: ThinkingChoice,
    onChoose: (ThinkingLevel?) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (!choice.offered) return
    Column(
        modifier = modifier
            .fillMaxWidth()
            .testTag(THINKING_ROW_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
    ) {
        Text(
            text = taffyString(R.string.taffy_providers_thinking_title),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
        )
        Row(
            modifier = Modifier
                .horizontalScroll(rememberScrollState())
                .selectableGroup(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            ThinkingPill(level = null, choice = choice, onChoose = onChoose)
            choice.rungs.forEach { rung ->
                ThinkingPill(level = rung, choice = choice, onChoose = onChoose)
            }
        }
        Text(
            text = taffyString(R.string.taffy_providers_thinking_note),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(THINKING_NOTE_TEST_TAG),
        )
    }
}

/** One pill. A null [level] is Auto, which is the absence of a preference. */
@Composable
private fun ThinkingPill(
    level: ThinkingLevel?,
    choice: ThinkingChoice,
    onChoose: (ThinkingLevel?) -> Unit,
) {
    val label = taffyString(thinkingLabel(level))
    val chosen = choice.stands(level)
    Text(
        text = label,
        style = TaffyTheme.typography.label,
        color = if (chosen) TaffyTheme.colors.textPrimary else TaffyTheme.colors.textSecondary,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        modifier = Modifier
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .clip(TaffyTheme.shapes.pill)
            .background(
                if (chosen) TaffyTheme.colors.surfaceRaised else TaffyTheme.colors.surfaceSunken,
            )
            .border(
                TaffyBorders.standard,
                if (chosen) TaffyTheme.colors.outline else TaffyTheme.colors.hairline,
                TaffyTheme.shapes.pill,
            )
            .selectable(selected = chosen, role = Role.RadioButton) { onChoose(level) }
            .padding(
                horizontal = TaffyTheme.spacing.snug,
                vertical = TaffyTheme.spacing.snug,
            )
            .testTag("$THINKING_PILL_TEST_TAG_PREFIX${thinkingTag(level)}")
            .semantics { contentDescription = label },
    )
}

/**
 * The name for one rung, or for Auto.
 *
 * Auto has its own string rather than borrowing `Off`'s: they are the two
 * answers this control exists to keep apart, and one label for both would put
 * the confusion back in the place the type removed it from.
 */
internal fun thinkingLabel(level: ThinkingLevel?): Int = when (level) {
    null -> R.string.taffy_providers_thinking_auto
    ThinkingLevel.OFF -> R.string.taffy_providers_thinking_off
    ThinkingLevel.MINIMAL -> R.string.taffy_providers_thinking_minimal
    ThinkingLevel.LOW -> R.string.taffy_providers_thinking_low
    ThinkingLevel.MEDIUM -> R.string.taffy_providers_thinking_medium
    ThinkingLevel.HIGH -> R.string.taffy_providers_thinking_high
    ThinkingLevel.XHIGH -> R.string.taffy_providers_thinking_xhigh
    ThinkingLevel.MAX -> R.string.taffy_providers_thinking_max
}

/** The stable half of a pill's test tag. Auto is named, never spelled `null`. */
internal fun thinkingTag(level: ThinkingLevel?): String = level?.name?.lowercase() ?: "auto"

/** The tags screen SCR-417's thinking control names. */
const val THINKING_ROW_TEST_TAG: String = "model_thinking_row"
const val THINKING_NOTE_TEST_TAG: String = "model_thinking_note"
const val THINKING_PILL_TEST_TAG_PREFIX: String = "model_thinking_"
