// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * A 48 dp sunken search field matching product chrome, not Material's outline.
 *
 * [placeholder] is the field's name. Clearing is a 48 dp control whose spoken
 * name the caller may replace; the default lives in this module.
 */
@Composable
fun TaffySearchField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier,
    clearContentDescription: String? = null,
    testTag: String? = null,
) {
    val keyboard = LocalSoftwareKeyboardController.current
    val interactionSource = remember { MutableInteractionSource() }
    val focused by interactionSource.collectIsFocusedAsState()
    val spokenClear = clearContentDescription ?: taffyString(R.string.taffy_search_clear)
    val shape = TaffyTheme.shapes.row
    val borderWidth = if (focused) TaffyBorders.emphasis else TaffyBorders.standard
    val borderColor = if (focused) TaffyTheme.colors.focusRing else TaffyTheme.colors.outline

    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        singleLine = true,
        textStyle = TaffyTheme.typography.body.copy(color = TaffyTheme.colors.textPrimary),
        cursorBrush = SolidColor(TaffyTheme.colors.textPrimary),
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
        keyboardActions = KeyboardActions(onSearch = { keyboard?.hide() }),
        interactionSource = interactionSource,
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .clip(shape)
            .background(TaffyTheme.colors.surfaceSunken)
            .border(borderWidth, borderColor, shape)
            .semantics { contentDescription = placeholder }
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
        decorationBox = { field ->
            Row(
                modifier = Modifier.padding(start = TaffyTheme.spacing.snug),
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
                    if (value.isEmpty()) {
                        Text(
                            text = placeholder,
                            style = TaffyTheme.typography.body,
                            color = TaffyTheme.colors.textSecondary,
                        )
                    }
                    field()
                }
                if (value.isNotEmpty()) {
                    Box(
                        modifier = Modifier
                            .size(TaffyTheme.spacing.minimumTouchTarget)
                            .clickable(role = Role.Button, onClick = { onValueChange("") })
                            .semantics { contentDescription = spokenClear },
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            imageVector = TaffyIcon.X,
                            contentDescription = null,
                            tint = TaffyTheme.colors.textSecondary,
                            modifier = Modifier.size(SearchIconSize),
                        )
                    }
                }
            }
        },
    )
}

private val SearchIconSize = 20.dp
