// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun SkillDetailPreview() {
    TaffyPreview(darkTheme = false) {
        SkillDetailContent(
            state = projectSkillDetail(
                SkillsRepository.Snapshot(
                    availability = SkillsRepository.Availability.READY,
                    skills = SkillsRepository.previewBuiltIns(),
                ),
                SkillsRepository.FORM_ASSISTANT,
            ),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun SkillDetailOffDarkPreview() {
    TaffyPreview(darkTheme = true) {
        SkillDetailContent(
            state = SkillDetailUiState(
                availability = SkillsRepository.Availability.READY,
                skill = SkillDetailUiState.Skill(
                    id = SkillsRepository.LIBRARY_BUILDER,
                    enabled = false,
                    builtIn = true,
                    mayUse = listOf(SkillsRepository.MayUse.LIBRARY),
                ),
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun SkillDetailMissingPreview() {
    TaffyPreview(darkTheme = false) {
        SkillDetailContent(
            state = SkillDetailUiState(availability = SkillsRepository.Availability.READY),
            onIntent = {},
        )
    }
}
