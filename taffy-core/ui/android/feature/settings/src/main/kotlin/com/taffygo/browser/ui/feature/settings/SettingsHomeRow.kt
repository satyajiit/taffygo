// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One destination in a settings group: the overflow-menu well, a wrapping
 * title, a one-or-two-line summary, and a chevron.
 */
@Composable
fun SettingsHomeRow(
    title: String,
    summary: String,
    icon: ImageVector,
    testTag: String,
    selected: Boolean,
    accentSelected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    leading: @Composable (() -> Unit)? = null,
    enabled: Boolean = true,
    showChevron: Boolean = true,
) {
    val description = taffyString(R.string.taffy_settings_section_description, title, summary)
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val wash = when {
        selected && accentSelected -> TaffyTheme.colors.accentWash
        selected || pressed -> TaffyTheme.colors.surfaceSunken
        else -> Color.Transparent
    }
    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(wash)
            .then(
                if (enabled) {
                    Modifier.clickable(
                        interactionSource = interaction,
                        indication = null,
                        role = Role.Button,
                        onClick = onClick,
                    )
                } else {
                    Modifier
                },
            )
            .heightIn(min = SettingsRowMinHeight)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            )
            .testTag(testTag)
            .semantics(mergeDescendants = true) {
                contentDescription = description
                this.selected = selected
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (leading != null) {
            leading()
        } else {
            SettingsGlyph(icon)
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.title.copy(fontWeight = FontWeight.W500),
                color = TaffyTheme.colors.textPrimary,
            )
            if (summary.isNotEmpty()) {
                Text(
                    text = summary,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if (showChevron) {
            Icon(
                imageVector = TaffyIcon.CaretRight,
                contentDescription = null,
                tint = TaffyTheme.colors.hairline,
                modifier = Modifier.size(ChevronSize),
            )
        }
    }
}

/**
 * Phosphor in the same sunken well the overflow menu uses, one ink for every
 * row.
 */
@Composable
internal fun SettingsGlyph(
    icon: ImageVector,
    modifier: Modifier = Modifier,
) {
    TaffyGlyphFrame(modifier = modifier) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = TaffyTheme.colors.textPrimary,
            modifier = Modifier.size(SettingsGlyphSize),
        )
    }
}

/**
 * Home destinations as OpenAlly labeled cards: an uppercase eyebrow and a
 * grouped list. Search results skip the eyebrows and sit in one card.
 */
@Composable
internal fun SettingsHomeHits(
    hits: List<SettingsSearchHit>,
    selected: SettingsSection?,
    grouped: Boolean,
    onOpen: (TaffyDestination) -> Unit,
    modifier: Modifier = Modifier,
) {
    val bySection = hits.associateBy { it.section }
    val blocks = if (grouped) {
        SettingsHomeGroup.entries.map { group ->
            group.titleRes to group.members.mapNotNull { bySection[it] }
        }
    } else {
        listOf(null to hits)
    }
    Column(
        modifier = modifier.testTag(SETTINGS_LIST_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.section),
    ) {
        blocks.forEach { (titleRes, rows) ->
            if (rows.isEmpty()) return@forEach
            Column(
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            ) {
                if (titleRes != null) {
                    SettingsHomeEyebrow(title = taffyString(titleRes))
                }
                TaffyGroupedCard {
                    rows.forEachIndexed { index, hit ->
                        SettingsHomeRow(
                            title = hit.title,
                            summary = hit.summary,
                            icon = hit.section.icon,
                            testTag = "$SETTINGS_SECTION_TEST_TAG_PREFIX${hit.section.name}",
                            selected = hit.section == selected,
                            accentSelected = hit.section.usesTaffyAccent(),
                            onClick = { onOpen(hit.destination) },
                        )
                        if (index < rows.lastIndex) {
                            TaffyGroupedCardDivider()
                        }
                    }
                }
            }
        }
    }
}

/** OpenAlly's 12/600 uppercase section label above a settings card. */
@Composable
internal fun SettingsHomeEyebrow(title: String, modifier: Modifier = Modifier) {
    Text(
        text = title.uppercase(),
        style = TaffyTheme.typography.caption.copy(
            fontWeight = FontWeight.W600,
            letterSpacing = 1.2.sp,
        ),
        color = TaffyTheme.colors.textSecondary,
        modifier = modifier
            .padding(start = TaffyTheme.spacing.step)
            .semantics { heading() },
    )
}

internal fun SettingsSection.usesTaffyAccent(): Boolean = when (this) {
    SettingsSection.TAFFY,
    SettingsSection.WHAT_TAFFY_CAN_DO,
    SettingsSection.PERSONALITY,
    SettingsSection.AI_AND_PROVIDERS,
    -> true
    else -> false
}

internal val SettingsGlyphSize = 21.dp

private val SettingsRowMinHeight = 72.dp
private val ChevronSize = 18.dp
