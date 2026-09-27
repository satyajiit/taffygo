// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffySegmentedControl

/** One Personality axis. Choosing an end never grants a permission. */
@Composable
internal fun TuningAxis(
    title: String,
    options: List<String>,
    selectedIndex: Int,
    testTag: String,
    optionPrefix: String,
    onSelect: (Int) -> Unit,
) {
    TaffyObjectCard {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            TaffySectionHeader(title = title)
            TaffySegmentedControl(
                options = options,
                selectedIndex = selectedIndex,
                onSelect = onSelect,
                optionModifier = { index -> Modifier.testTag("$optionPrefix$index") },
                modifier = Modifier.testTag(testTag),
            )
        }
    }
}
