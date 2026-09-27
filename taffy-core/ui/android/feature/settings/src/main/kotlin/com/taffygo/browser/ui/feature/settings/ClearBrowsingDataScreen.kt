// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffySegmentedControl
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-207 — clear browsing data. */
@Composable
fun ClearBrowsingDataScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: ClearBrowsingDataViewModel =
        screenViewModel(TaffyDestination.ClearBrowsingData)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    ClearBrowsingDataContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun ClearBrowsingDataContent(
    state: ClearBrowsingDataUiState,
    onIntent: (ClearBrowsingDataIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val ranges = ClearBrowsingDataUiState.Range.entries
    TaffyScreen(
        destination = TaffyDestination.ClearBrowsingData,
        title = taffyString(R.string.taffy_clear_title),
        onBack = onBack,
        modifier = modifier,
    ) {
        TaffySectionHeader(title = taffyString(R.string.taffy_clear_range_heading))
        TaffySegmentedControl(
            options = ranges.map { taffyString(rangeName(it)) },
            selectedIndex = ranges.indexOf(state.range),
            onSelect = { index ->
                onIntent(ClearBrowsingDataIntent.SelectRange(ranges[index]))
            },
            optionModifier = { index ->
                Modifier.testTag("$CLEAR_RANGE_TEST_TAG_PREFIX${ranges[index].name}")
            },
            modifier = Modifier.testTag(CLEAR_RANGE_LIST_TEST_TAG),
        )
        TaffySectionHeader(title = taffyString(R.string.taffy_clear_classes_heading))
        TaffyGroupedCard(testTag = CLEAR_CLASS_LIST_TEST_TAG) {
            val supported = ClearBrowsingDataUiState.DataClass.entries.filter {
                it in state.supportedClasses
            }
            supported.forEachIndexed { index, dataClass ->
                ClearClassRow(
                    dataClass = dataClass,
                    checked = dataClass in state.classes,
                    onToggle = { onIntent(ClearBrowsingDataIntent.ToggleClass(dataClass)) },
                )
                if (index < supported.lastIndex) {
                    TaffyGroupedCardDivider()
                }
            }
        }
        Text(
            text = taffyString(R.string.taffy_clear_workspaces_stay),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(CLEAR_WORKSPACES_TEST_TAG),
        )
        if (!state.available) {
            Text(
                text = taffyString(R.string.taffy_clear_unavailable),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(CLEAR_UNAVAILABLE_TEST_TAG),
            )
        }
        if (state.failed) {
            Text(
                text = taffyString(R.string.taffy_clear_failed),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.danger,
                modifier = Modifier.testTag(CLEAR_FAILED_TEST_TAG),
            )
        }
        TaffyDangerButton(
            label = taffyString(R.string.taffy_clear_confirm),
            onClick = { onIntent(ClearBrowsingDataIntent.Confirm) },
            enabled = state.canClear,
            testTag = CLEAR_CONFIRM_TEST_TAG,
        )
    }
    if (state.confirming) {
        TaffyBottomSheet(
            title = taffyString(R.string.taffy_clear_confirm_title),
            onDismissRequest = { onIntent(ClearBrowsingDataIntent.DismissConfirm) },
            testTag = CLEAR_CONFIRM_SHEET_TEST_TAG,
        ) {
            Text(
                text = taffyString(R.string.taffy_clear_confirm_body),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            TaffyDangerButton(
                label = taffyString(R.string.taffy_clear_confirm),
                onClick = { onIntent(ClearBrowsingDataIntent.Submit) },
                enabled = state.canClear && !state.submitting,
                testTag = CLEAR_SUBMIT_TEST_TAG,
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_clear_keep),
                onClick = { onIntent(ClearBrowsingDataIntent.DismissConfirm) },
            )
        }
    }
}

@Composable
private fun ClearClassRow(
    dataClass: ClearBrowsingDataUiState.DataClass,
    checked: Boolean,
    onToggle: () -> Unit,
) {
    val name = taffyString(className(dataClass))
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(TaffyTheme.spacing.screenMargin)
            .testTag("$CLEAR_CLASS_TEST_TAG_PREFIX${dataClass.name}"),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = name,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = taffyString(
                    if (checked) R.string.taffy_clear_class_on else R.string.taffy_clear_class_off,
                ),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        TaffySwitch(
            checked = checked,
            onCheckedChange = { onToggle() },
            accessibleName = name,
            testTag = "$CLEAR_CLASS_SWITCH_TEST_TAG_PREFIX${dataClass.name}",
        )
    }
}

private fun rangeName(range: ClearBrowsingDataUiState.Range): Int = when (range) {
    ClearBrowsingDataUiState.Range.LAST_HOUR -> R.string.taffy_clear_range_hour
    ClearBrowsingDataUiState.Range.LAST_DAY -> R.string.taffy_clear_range_day
    ClearBrowsingDataUiState.Range.LAST_WEEK -> R.string.taffy_clear_range_week
    ClearBrowsingDataUiState.Range.ALL_TIME -> R.string.taffy_clear_range_all
}

private fun className(dataClass: ClearBrowsingDataUiState.DataClass): Int = when (dataClass) {
    ClearBrowsingDataUiState.DataClass.HISTORY -> R.string.taffy_clear_class_history
    ClearBrowsingDataUiState.DataClass.COOKIES -> R.string.taffy_clear_class_cookies
    ClearBrowsingDataUiState.DataClass.CACHED_FILES -> R.string.taffy_clear_class_cache
    ClearBrowsingDataUiState.DataClass.TIME_ON_SITES -> R.string.taffy_clear_class_time
}

const val CLEAR_RANGE_LIST_TEST_TAG: String = "clear_ranges"
const val CLEAR_RANGE_TEST_TAG_PREFIX: String = "clear_range_"
const val CLEAR_CLASS_LIST_TEST_TAG: String = "clear_classes"
const val CLEAR_CLASS_TEST_TAG_PREFIX: String = "clear_class_"
const val CLEAR_CLASS_SWITCH_TEST_TAG_PREFIX: String = "clear_class_switch_"
const val CLEAR_WORKSPACES_TEST_TAG: String = "clear_workspaces"
const val CLEAR_UNAVAILABLE_TEST_TAG: String = "clear_unavailable"
const val CLEAR_FAILED_TEST_TAG: String = "clear_failed"
const val CLEAR_CONFIRM_TEST_TAG: String = "clear_confirm"
const val CLEAR_CONFIRM_SHEET_TEST_TAG: String = "clear_confirm_sheet"
const val CLEAR_SUBMIT_TEST_TAG: String = "clear_submit"
