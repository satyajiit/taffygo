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
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Find in page (SCR-106): a 48 dp field, count, previous, next, and close.
 *
 * Drawn in the bottom chrome column, immediately above the action row. The
 * page surface is never translated, clipped, or scaled to make room for it.
 */
@Composable
internal fun FindInPageOverlay(
    state: FindInPageUiState,
    onIntent: (FindInPageIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { runCatching { focus.requestFocus() } }
    val placeholder = taffyString(R.string.taffy_find_placeholder)
    Column(
        modifier = modifier
            .taffyChromeWidth()
            .padding(horizontal = TaffyTheme.spacing.screenMargin)
            .testTag(FIND_OVERLAY_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            FindField(
                value = state.query,
                placeholder = placeholder,
                onValueChange = { onIntent(FindInPageIntent.QueryChanged(it)) },
                onNext = {
                    if (state.canMove) onIntent(FindInPageIntent.Next)
                },
                modifier = Modifier
                    .weight(1f)
                    .focusRequester(focus),
            )
            if (state.showsCount) {
                val count = taffyString(
                    R.string.taffy_find_count,
                    state.activeIndex,
                    state.matchCount,
                )
                Text(
                    text = count,
                    style = TaffyTheme.typography.numeric,
                    color = TaffyTheme.colors.textPrimary,
                    modifier = Modifier
                        .testTag(FIND_COUNT_TEST_TAG)
                        .semantics { contentDescription = count },
                )
            }
            BrowserChromeButton(
                icon = TaffyIcon.ArrowLeft,
                contentDescription = taffyString(R.string.taffy_find_previous),
                enabled = state.canMove,
                onClick = { onIntent(FindInPageIntent.Previous) },
                testTag = FIND_PREVIOUS_TEST_TAG,
                targetSize = TaffyTheme.spacing.minimumTouchTarget,
            )
            BrowserChromeButton(
                icon = TaffyIcon.ArrowRight,
                contentDescription = taffyString(R.string.taffy_find_next),
                enabled = state.canMove,
                onClick = { onIntent(FindInPageIntent.Next) },
                testTag = FIND_NEXT_TEST_TAG,
                targetSize = TaffyTheme.spacing.minimumTouchTarget,
            )
            BrowserChromeButton(
                icon = TaffyIcon.X,
                contentDescription = taffyString(R.string.taffy_find_close),
                onClick = { onIntent(FindInPageIntent.Close) },
                testTag = FIND_CLOSE_TEST_TAG,
                targetSize = TaffyTheme.spacing.minimumTouchTarget,
            )
        }
        if (!state.available) {
            Text(
                text = taffyString(R.string.taffy_find_unavailable),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(FIND_UNAVAILABLE_TEST_TAG),
            )
        }
    }
}

@Composable
private fun FindField(
    value: String,
    placeholder: String,
    onValueChange: (String) -> Unit,
    onNext: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val shape = TaffyTheme.shapes.row
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        singleLine = true,
        textStyle = TaffyTheme.typography.body.copy(color = TaffyTheme.colors.textPrimary),
        cursorBrush = SolidColor(TaffyTheme.colors.textPrimary),
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
        keyboardActions = KeyboardActions(onSearch = { onNext() }),
        modifier = modifier
            .height(TaffyTheme.spacing.minimumTouchTarget)
            .clip(shape)
            .background(TaffyTheme.colors.surfaceSunken)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, shape)
            .semantics { contentDescription = placeholder }
            .testTag(FIND_FIELD_TEST_TAG),
        decorationBox = { field ->
            Box(
                modifier = Modifier.padding(horizontal = TaffyTheme.spacing.snug),
                contentAlignment = Alignment.CenterStart,
            ) {
                if (value.isEmpty()) {
                    Text(
                        text = placeholder,
                        style = TaffyTheme.typography.body,
                        color = TaffyTheme.colors.textSecondary,
                    )
                }
                field()
            }
        },
    )
}

internal fun FindInPageIntent.toBrowserMainIntent(): BrowserMainIntent = when (this) {
    is FindInPageIntent.QueryChanged -> BrowserMainIntent.FindQueryChanged(query)
    FindInPageIntent.Next -> BrowserMainIntent.FindNext
    FindInPageIntent.Previous -> BrowserMainIntent.FindPrevious
    FindInPageIntent.Close -> BrowserMainIntent.DismissFindInPage
}

/** The find overlay, which screen SCR-106's semantics tests name. */
const val FIND_OVERLAY_TEST_TAG: String = "find_in_page"

/** The phrase field. */
const val FIND_FIELD_TEST_TAG: String = "find_in_page_field"

/** The tabular match count, absent while the field is empty. */
const val FIND_COUNT_TEST_TAG: String = "find_in_page_count"

/** Jump to the previous match. */
const val FIND_PREVIOUS_TEST_TAG: String = "find_in_page_previous"

/** Jump to the next match. */
const val FIND_NEXT_TEST_TAG: String = "find_in_page_next"

/** Close the overlay. */
const val FIND_CLOSE_TEST_TAG: String = "find_in_page_close"

/** The honest caption while the engine cannot search. */
const val FIND_UNAVAILABLE_TEST_TAG: String = "find_in_page_unavailable"
