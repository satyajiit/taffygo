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
private fun WhatHappenedUnavailablePreview() {
    TaffyPreview(darkTheme = false) {
        WhatHappenedContent(state = WhatHappenedUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun WhatHappenedReadyDarkPreview() {
    TaffyPreview(darkTheme = true) {
        WhatHappenedContent(
            state = WhatHappenedUiState(
                availability = YouSurfaceAvailability.READY,
                blockedRequestsThisWeek = 1_204,
                days = listOf(
                    WhatHappenedUiState.Day(
                        epochDay = 19_700,
                        events = listOf(
                            WhatHappenedRepository.Event(
                                id = "e1",
                                kind = WhatHappenedRepository.Kind.WORKSPACE_RESULT,
                                epochMillis = 1_700_000_000_000L,
                                epochDay = 19_700,
                                sourceCount = 4,
                            ),
                        ),
                    ),
                ),
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun WhatHappenedEmptyPreview() {
    TaffyPreview(darkTheme = false) {
        WhatHappenedContent(
            state = WhatHappenedUiState(availability = YouSurfaceAvailability.READY),
            onIntent = {},
        )
    }
}
