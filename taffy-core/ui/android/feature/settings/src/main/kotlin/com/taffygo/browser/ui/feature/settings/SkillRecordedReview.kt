// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySavedFlowReview
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.taffyString

/** Recorded drafts never share the ordinary enabled switch until explicitly reviewed. */
@Composable
internal fun SkillRecordedReview(
    skill: SkillDetailUiState.Skill,
    acceptance: SkillsRepository.MutationResult?,
    onIntent: (SkillDetailIntent) -> Unit,
    loading: Boolean = false,
    failed: Boolean = false,
) {
    TaffySectionHeader(title = taffyString(R.string.taffy_skill_review_title))
    val review = skill.review
    if (review == null) {
        Text(
            text = taffyString(R.string.taffy_skill_review_missing),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyPrimaryButton(
            label = taffyString(if (loading) R.string.taffy_skill_review_loading else R.string.taffy_skill_review_load),
            onClick = { onIntent(SkillDetailIntent.LoadRecordedReview) },
            enabled = !loading,
            testTag = "skill_recorded_load",
        )
        if (failed) Text(taffyString(R.string.taffy_skill_review_load_failed), color = TaffyTheme.colors.danger)
        return
    }
    TaffySavedFlowReview(review)
    val pending = acceptance == SkillsRepository.MutationResult.SUBMITTED
    TaffyPrimaryButton(
        label = taffyString(if (pending) R.string.taffy_skill_review_saving else R.string.taffy_skill_review_save),
        onClick = { onIntent(SkillDetailIntent.AcceptRecorded(review)) },
        enabled = !pending,
        testTag = SKILL_RECORDED_ACCEPT_TEST_TAG,
    )
    if (acceptance != null && !pending) {
        Text(
            text = taffyString(R.string.taffy_skill_review_failed),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.danger,
        )
    }
}

const val SKILL_RECORDED_ACCEPT_TEST_TAG: String = "skill_recorded_accept"
