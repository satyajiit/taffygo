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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyGroupedCardItems
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** One job heading and the installed abilities under it. */
internal fun LazyListScope.skillsListGroup(
    group: SkillsListUiState.Group,
    onIntent: (SkillsListIntent) -> Unit,
) {
    item(
        key = "skill-group-${group.group.name}",
        contentType = "skill-group",
    ) {
        TaffySectionHeader(
            title = taffyString(skillGroupTitleRes(group.group)),
            modifier = Modifier.testTag("$SKILLS_GROUP_TEST_TAG_PREFIX${group.group.name}"),
        )
    }
    taffyGroupedCardItems(
        values = group.skills,
        key = SkillsListUiState.Row::id,
        contentType = { "skill" },
    ) { row ->
        SkillAbilityRow(row = row, onIntent = onIntent)
    }
}

/** Title and summary open details; the switch is a separate control. */
@Composable
internal fun SkillAbilityRow(
    row: SkillsListUiState.Row,
    onIntent: (SkillsListIntent) -> Unit,
) {
    val title = skillTitleRes(row.id)?.let { taffyString(it) } ?: row.name
    val summary = skillSummaryRes(row.id)?.let { taffyString(it) }
        ?: taffyPlural(
            R.plurals.taffy_site_skill_summary,
            row.stepCount.toInt(),
            row.origin.orEmpty(),
            row.stepCount.toInt(),
        )
    val stateWord = if (row.readiness == SkillsRepository.Readiness.READY) {
        taffyString(if (row.enabled) R.string.taffy_skills_on else R.string.taffy_skills_off)
    } else {
        taffyString(skillReadinessRes(row.readiness))
    }
    val opens = taffyString(R.string.taffy_skills_row_opens, title, summary, stateWord)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = AbilityRowMinHeight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier
                .weight(1f)
                .clickable(onClick = { onIntent(SkillsListIntent.Open(row.id)) })
                .heightIn(min = AbilityRowMinHeight)
                .padding(
                    start = TaffyTheme.spacing.screenMargin,
                    top = TaffyTheme.spacing.snug,
                    bottom = TaffyTheme.spacing.snug,
                )
                .testTag("$SKILL_ROW_TEST_TAG_PREFIX${row.id}")
                .semantics(mergeDescendants = true) {
                    contentDescription = opens
                    role = Role.Button
                },
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SettingsGlyph(skillIcon(row.id))
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
        }
        if (row.needsRecordedReview) {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_skill_review_open),
                onClick = { onIntent(SkillsListIntent.Open(row.id)) },
                modifier = Modifier.padding(end = TaffyTheme.spacing.snug),
            )
        } else TaffySwitch(
            checked = row.enabled,
            onCheckedChange = { onIntent(SkillsListIntent.Toggle(row.id)) },
            accessibleName = title,
            enabled = row.readiness == SkillsRepository.Readiness.READY,
            modifier = Modifier.padding(end = TaffyTheme.spacing.screenMargin),
            testTag = "$SKILL_SWITCH_TEST_TAG_PREFIX${row.id}",
        )
    }
}

private val AbilityRowMinHeight = 72.dp
