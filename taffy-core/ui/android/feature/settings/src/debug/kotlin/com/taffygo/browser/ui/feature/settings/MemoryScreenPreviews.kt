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
private fun MemoryEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        MemoryContent(state = MemoryUiState(processScoped = true), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun MemoryListDarkPreview() {
    TaffyPreview(darkTheme = true) {
        MemoryContent(
            state = projectMemory(
                snapshot = MemoryRepository.Snapshot(
                    availability = YouSurfaceAvailability.READY,
                    notes = listOf(
                        MemoryRepository.Note(
                            id = "m1",
                            statement = "Prefers window seats",
                            source = MemoryRepository.Source.YOU_WROTE,
                            addedEpochDay = 20_300,
                        ),
                        MemoryRepository.Note(
                            id = "m2",
                            statement = "Answers in short bullet points",
                            source = MemoryRepository.Source.TAFFY_NOTICED,
                            addedEpochDay = 20_301,
                            workspaceName = "TV",
                        ),
                    ),
                    processScoped = true,
                ),
                query = "",
                editor = null,
                confirmDelete = false,
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun MemoryEditorPreview() {
    TaffyPreview(darkTheme = false) {
        MemoryContent(
            state = MemoryUiState(
                processScoped = true,
                editor = MemoryUiState.Editor(text = "Prefers window seats"),
            ),
            onIntent = {},
        )
    }
}
