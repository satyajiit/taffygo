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
private fun SkillsListPreview() {
    TaffyPreview(darkTheme = false) {
        SkillsListContent(
            state = projectSkillsList(
                SkillsRepository.Snapshot(
                    availability = SkillsRepository.Availability.READY,
                    skills = SkillsRepository.previewBuiltIns(),
                ),
                query = "",
            ),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun SkillsListDarkPreview() {
    TaffyPreview(darkTheme = true) {
        SkillsListContent(
            state = projectSkillsList(
                SkillsRepository.Snapshot(
                    availability = SkillsRepository.Availability.READY,
                    skills = SkillsRepository.previewBuiltIns().map { skill ->
                        if (skill.id == SkillsRepository.FORM_ASSISTANT) {
                            skill.copy(enabled = false)
                        } else {
                            skill
                        }
                    },
                ),
                query = "",
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun SkillsListUnavailablePreview() {
    TaffyPreview(darkTheme = false) {
        SkillsListContent(
            state = SkillsListUiState(
                availability = SkillsRepository.Availability.UNAVAILABLE,
            ),
            onIntent = {},
        )
    }
}
