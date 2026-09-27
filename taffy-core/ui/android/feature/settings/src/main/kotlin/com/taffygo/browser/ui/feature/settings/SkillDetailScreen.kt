// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.TaffySavedFlowReview
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-602 — one installed ability. */
@Composable
fun SkillDetailScreen(
    destination: TaffyDestination.SkillDetail,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: SkillDetailViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    SkillDetailContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun SkillDetailContent(
    state: SkillDetailUiState,
    onIntent: (SkillDetailIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val title = state.skill?.let { skill ->
        skillTitleRes(skill.id)?.let { taffyString(it) } ?: skill.name
    } ?: taffyString(R.string.taffy_skill_detail_title)
    TaffyScreen(
        destination = TaffyDestination.SkillDetail(state.skill?.id.orEmpty()),
        title = title,
        onBack = onBack,
        modifier = modifier,
    ) {
        when {
            state.availability == SkillsRepository.Availability.LOADING -> TaffyHubSkeletonList(
                description = taffyString(R.string.taffy_skill_detail_loading),
                testTag = SKILL_DETAIL_LOADING_TEST_TAG,
            )
            state.availability == SkillsRepository.Availability.UNAVAILABLE -> TaffyEmptyState(
                title = taffyString(R.string.taffy_skills_unavailable_title),
                body = taffyString(R.string.taffy_skills_unavailable_body),
                leading = { SkillDetailEmptyGlyph() },
            )
            state.skill == null -> TaffyEmptyState(
                title = taffyString(R.string.taffy_skill_missing_title),
                body = taffyString(R.string.taffy_skill_missing_body),
                leading = { SkillDetailEmptyGlyph() },
            )
            else -> SkillDetailReady(state.skill, state.removeResult, state.acceptResult, onIntent, state.reviewLoading, state.reviewFailed)
        }
    }
}

@Composable
private fun SkillDetailReady(
    skill: SkillDetailUiState.Skill,
    removeResult: SkillsRepository.RemoveResult?,
    acceptResult: SkillsRepository.MutationResult?,
    onIntent: (SkillDetailIntent) -> Unit,
    reviewLoading: Boolean = false,
    reviewFailed: Boolean = false,
) {
    val summary = skillSummaryRes(skill.id)?.let { taffyString(it) }
        ?: taffyPlural(
            R.plurals.taffy_site_skill_summary,
            skill.stepCount.toInt(),
            skill.origin.orEmpty(),
            skill.stepCount.toInt(),
        )
    TaffySectionHeader(title = taffyString(R.string.taffy_skill_detail_does))
    Text(
        text = summary,
        style = TaffyTheme.typography.body,
        color = TaffyTheme.colors.textPrimary,
        modifier = Modifier.testTag(SKILL_DETAIL_DOES_TEST_TAG),
    )
    TaffySectionHeader(title = taffyString(R.string.taffy_skill_detail_may_use))
    Column(
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        modifier = Modifier.testTag(SKILL_DETAIL_MAY_USE_TEST_TAG),
    ) {
        skill.mayUse.forEach { mayUse ->
            Text(
                text = taffyString(skillMayUseRes(mayUse)),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
        }
    }
    Text(
        text = taffyString(R.string.taffy_skill_approvals),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier.testTag(SKILL_DETAIL_APPROVALS_TEST_TAG),
    )
    if (skill.builtIn) {
        Text(
            text = taffyString(R.string.taffy_skill_detail_comes_with),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(SKILL_DETAIL_VERSION_TEST_TAG),
        )
    } else {
        Text(
            text = taffyPlural(
                R.plurals.taffy_site_skill_version,
                skill.stepCount.toInt(),
                skill.version.toInt(),
                skill.stepCount.toInt(),
            ),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(SKILL_DETAIL_VERSION_TEST_TAG),
        )
    }
    if (skill.readiness != SkillsRepository.Readiness.READY) {
        Text(
            text = taffyString(skillReadinessRes(skill.readiness)),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.danger,
        )
    }
    if (skill.needsRecordedReview) {
        SkillRecordedReview(skill, acceptResult, onIntent, reviewLoading, reviewFailed)
    } else {
        skill.review?.let { TaffySavedFlowReview(it) }
        if (skill.recorded && skill.review == null) {
            SkillRecordedReview(skill, acceptResult, onIntent, reviewLoading, reviewFailed)
        }
        SkillDetailToggle(skill, onIntent)
    }
    SkillDetailRemove(skill, removeResult, onIntent)
}

@Composable
private fun SkillDetailToggle(
    skill: SkillDetailUiState.Skill,
    onIntent: (SkillDetailIntent) -> Unit,
) {
    val stateWord = taffyString(
        if (skill.enabled) R.string.taffy_skills_on else R.string.taffy_skills_off,
    )
    val title = taffyString(R.string.taffy_skill_enabled_toggle)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .testTag(SKILL_DETAIL_TOGGLE_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = stateWord,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        TaffySwitch(
            checked = skill.enabled,
            onCheckedChange = { onIntent(SkillDetailIntent.Toggle) },
            accessibleName = title,
            enabled = skill.readiness == SkillsRepository.Readiness.READY,
            testTag = SKILL_DETAIL_SWITCH_TEST_TAG,
        )
    }
}

@Composable
private fun SkillDetailRemove(
    skill: SkillDetailUiState.Skill,
    removeResult: SkillsRepository.RemoveResult?,
    onIntent: (SkillDetailIntent) -> Unit,
) {
    val canRemove = !skill.builtIn &&
        removeResult != SkillsRepository.RemoveResult.UNAVAILABLE
    TaffySecondaryButton(
        label = taffyString(R.string.taffy_skill_remove),
        onClick = { onIntent(SkillDetailIntent.Remove) },
        enabled = canRemove,
        testTag = SKILL_DETAIL_REMOVE_TEST_TAG,
    )
    val reason = when {
        skill.builtIn -> R.string.taffy_skill_cannot_remove
        removeResult == SkillsRepository.RemoveResult.UNAVAILABLE ||
            removeResult == SkillsRepository.RemoveResult.CANNOT_REMOVE ->
            R.string.taffy_skill_remove_unavailable
        else -> null
    }
    if (reason != null) {
        Text(
            text = taffyString(reason),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(SKILL_DETAIL_CANNOT_REMOVE_TEST_TAG),
        )
    }
}

/** The tags screen SCR-602's semantics tests name. */
const val SKILL_DETAIL_LOADING_TEST_TAG: String = "skill_detail_loading"
const val SKILL_DETAIL_DOES_TEST_TAG: String = "skill_detail_does"
const val SKILL_DETAIL_MAY_USE_TEST_TAG: String = "skill_detail_may_use"
const val SKILL_DETAIL_APPROVALS_TEST_TAG: String = "skill_detail_approvals"
const val SKILL_DETAIL_VERSION_TEST_TAG: String = "skill_detail_version"
const val SKILL_DETAIL_TOGGLE_TEST_TAG: String = "skill_detail_toggle"
const val SKILL_DETAIL_SWITCH_TEST_TAG: String = "skill_detail_switch"
const val SKILL_DETAIL_REMOVE_TEST_TAG: String = "skill_detail_remove"
const val SKILL_DETAIL_CANNOT_REMOVE_TEST_TAG: String = "skill_detail_cannot_remove"

@Composable
private fun SkillDetailEmptyGlyph() {
    Icon(
        imageVector = TaffyIcon.PuzzlePiece,
        contentDescription = null,
        tint = TaffyTheme.colors.textPrimary,
        modifier = Modifier.size(SettingsGlyphSize),
    )
}
