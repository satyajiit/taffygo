// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/** One engine in the picker: vendor mark, name, and a check when it is selected. */
@Composable
fun SearchEngineChoiceRow(
    choice: SearchEngineUiState.Choice,
    onSelect: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val name = taffyString(searchEngineNameRes(choice.id))
    val description = if (choice.selected) {
        taffyString(R.string.taffy_search_engine_choice_selected, name)
    } else {
        taffyString(R.string.taffy_search_engine_choice, name)
    }
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clickable(onClick = onSelect)
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            )
            .testTag("$SEARCH_ENGINE_ROW_TEST_TAG_PREFIX${choice.id.wireName}")
            .semantics(mergeDescendants = true) {
                contentDescription = description
                this.selected = choice.selected
                role = Role.RadioButton
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SearchEngineMark(markFile = choice.markFile)
        Text(
            text = name,
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.weight(1f),
        )
        if (choice.selected) {
            Icon(
                imageVector = TaffyIcon.Check,
                contentDescription = null,
                tint = TaffyTheme.colors.accentDeep,
                modifier = Modifier.size(CheckSize),
            )
        }
    }
}

const val SEARCH_ENGINE_ROW_TEST_TAG_PREFIX: String = "search_engine_row_"
private val CheckSize = 20.dp
