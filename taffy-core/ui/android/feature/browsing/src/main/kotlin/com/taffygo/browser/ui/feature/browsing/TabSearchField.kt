// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Mock 04's search control, in the same pill the language screen searches from.
 *
 * It filters what is already open — title and host — and reaches no history and
 * no network. That is the whole of what it does, and the placeholder says so:
 * a box on a tab switcher that looked like it searched the web would be a
 * second address bar wearing a magnifier.
 *
 * The keyboard offers Search rather than Done, and the grid filters on every
 * keystroke, so the action only gets the keyboard out of the results' way.
 */
@Composable
internal fun TabSearchField(query: String, onQueryChange: (String) -> Unit) {
    val keyboard = LocalSoftwareKeyboardController.current
    BasicTextField(
        value = query,
        onValueChange = onQueryChange,
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
        keyboardActions = KeyboardActions(onSearch = { keyboard?.hide() }),
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = SearchHeight)
            .clip(TaffyTheme.shapes.pill)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.pill)
            .testTag(TAB_SEARCH_TEST_TAG),
        singleLine = true,
        textStyle = TaffyTheme.typography.body.copy(color = TaffyTheme.colors.textPrimary),
        cursorBrush = SolidColor(TaffyTheme.colors.textPrimary),
        decorationBox = { field ->
            Row(
                modifier = Modifier.padding(horizontal = TaffyTheme.spacing.snug),
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(
                    imageVector = TaffyIcon.MagnifyingGlass,
                    contentDescription = null,
                    tint = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.size(SearchIconSize),
                )
                Box(modifier = Modifier.weight(1f)) {
                    if (query.isEmpty()) {
                        Text(
                            text = taffyString(R.string.taffy_tab_switcher_search),
                            style = TaffyTheme.typography.body,
                            color = TaffyTheme.colors.textSecondary,
                        )
                    }
                    field()
                }
            }
        },
    )
}

/** The field, for the semantics test that types into it. */
const val TAB_SEARCH_TEST_TAG: String = "tab_switcher_search"

private val SearchHeight = 44.dp
private val SearchIconSize = 16.dp
