// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.taffyString

/** How Taffy talks, what Taffy can do, and AI and providers — a list, never a grid. */
@Composable
internal fun TaffyHubDestinations(onIntent: (TaffySettingsIntent) -> Unit) {
    TaffyGroupedCard(testTag = TAFFY_HUB_LIST_TEST_TAG) {
        SettingsHomeRow(
            title = taffyString(R.string.taffy_taffy_hub_talk),
            summary = taffyString(R.string.taffy_taffy_hub_talk_summary),
            icon = TaffyIcon.ChatCircle,
            testTag = TAFFY_HUB_TALK_TEST_TAG,
            selected = false,
            accentSelected = true,
            onClick = { onIntent(TaffySettingsIntent.OpenPersonality) },
        )
        TaffyGroupedCardDivider()
        SettingsHomeRow(
            title = taffyString(R.string.taffy_taffy_hub_skills),
            summary = taffyString(R.string.taffy_taffy_hub_skills_summary),
            icon = TaffyIcon.PuzzlePiece,
            testTag = TAFFY_HUB_SKILLS_TEST_TAG,
            selected = false,
            accentSelected = true,
            onClick = { onIntent(TaffySettingsIntent.OpenSkills) },
        )
        TaffyGroupedCardDivider()
        SettingsHomeRow(
            title = taffyString(R.string.taffy_taffy_hub_ai),
            summary = taffyString(R.string.taffy_taffy_hub_ai_summary),
            icon = TaffyIcon.Key,
            testTag = TAFFY_HUB_AI_TEST_TAG,
            selected = false,
            accentSelected = true,
            onClick = { onIntent(TaffySettingsIntent.OpenAiProviders) },
        )
    }
}

/**
 * Suggestions while you type, with the sentence that has to be readable before
 * anybody turns it on (decision 0097 section 7).
 *
 * Every claim in the body is one of that decision's own: what is sent is the
 * composer's own text (section 4), it runs on the person's own credential
 * (section 2), and it belongs to no task and is journalled nowhere
 * (section 1).
 */
@Composable
internal fun TaffyHubComposerSuggestions(
    state: TaffySettingsUiState,
    onIntent: (TaffySettingsIntent) -> Unit,
) {
    val name = taffyString(R.string.taffy_taffy_hub_suggestions_title)
    TaffyGroupedCard(testTag = TAFFY_HUB_SUGGESTIONS_TEST_TAG) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(TaffyTheme.spacing.screenMargin)
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
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
                    text = taffyString(R.string.taffy_taffy_hub_suggestions_body),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.testTag(TAFFY_HUB_SUGGESTIONS_BODY_TEST_TAG),
                )
            }
            TaffySwitch(
                checked = state.composerSuggestions,
                onCheckedChange = {
                    onIntent(TaffySettingsIntent.ToggleComposerSuggestions)
                },
                accessibleName = name,
                testTag = TAFFY_HUB_SUGGESTIONS_SWITCH_TEST_TAG,
            )
        }
    }
}

/** One Taffy-owned row inside a grouped card. */
@Composable
internal fun TaffyHubRow(
    title: String,
    summary: String,
    icon: ImageVector,
    testTag: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    selected: Boolean = false,
) {
    val description = taffyString(R.string.taffy_taffy_hub_opens, title, summary)
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .heightIn(min = HubRowMinHeight)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            )
            .testTag(testTag)
            .semantics(mergeDescendants = true) {
                contentDescription = description
                this.selected = selected
                role = Role.Button
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SettingsGlyph(icon)
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = summary,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        Icon(
            imageVector = TaffyIcon.CaretRight,
            contentDescription = null,
            tint = TaffyTheme.colors.hairline,
            modifier = Modifier.size(ChevronSize),
        )
    }
}

/** Row-shaped skeletons so a wait looks like the list that is coming. */
@Composable
internal fun TaffyHubSkeletonList(
    description: String,
    testTag: String,
    rows: Int = 3,
) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        repeat(rows) { index ->
            TaffySkeleton(
                shape = TaffyTheme.shapes.row,
                accessibleDescription = if (index == 0) description else null,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(HubRowMinHeight)
                    .then(if (index == 0) Modifier.testTag(testTag) else Modifier),
            )
        }
    }
}

@Composable
internal fun taffyHubAccentInk(): Color =
    if (TaffyTheme.isDark) TaffyTheme.colors.accentText else TaffyTheme.colors.accentDeep

private val HubRowMinHeight = 72.dp
private val ChevronSize = 18.dp
