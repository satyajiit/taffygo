// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionBar
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyGroupedCardItems
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-206 — ad and tracker blocking. */
@Composable
fun FilteringSettingsScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: FilteringSettingsViewModel =
        screenViewModel(TaffyDestination.AdAndTrackerBlocking)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    FilteringSettingsContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half, which is what a preview and a semantics test render. */
@Composable
fun FilteringSettingsContent(
    state: FilteringSettingsUiState,
    onIntent: (FilteringSettingsIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyLazyScreen(
        destination = TaffyDestination.AdAndTrackerBlocking,
        title = taffyString(R.string.taffy_filtering_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = Modifier.testTag(FILTERING_LIST_TEST_TAG),
    ) {
        item(key = "filtering-showcase", contentType = "showcase") {
            FilteringShowcase(state = state, onIntent = onIntent)
        }
        exceptionsSection(state, onIntent)
    }
}

/**
 * The sites blocking leaves alone, each with the way back.
 *
 * While blocking is off the rows stay, read-only. Hiding them would contradict
 * this screen's own policy — the empty state deliberately reports "No
 * exceptions" while blocking is off — and reporting nothing when there *are*
 * some is worse than either policy alone. The list is a record a person needs
 * before turning blocking back on; only the button is a control, so only the
 * button is withdrawn.
 */
private fun LazyListScope.exceptionsSection(
    state: FilteringSettingsUiState,
    onIntent: (FilteringSettingsIntent) -> Unit,
) {
    item(key = "filtering-exceptions-heading", contentType = "heading") {
        TaffySectionBar(
            title = taffyString(R.string.taffy_filtering_exceptions),
            eyebrow = taffyString(R.string.taffy_filtering_exceptions_eyebrow),
            count = state.exceptionHosts.size.takeIf { it > 0 }?.let { taffyCount(it) },
            countDescription = state.exceptionHosts.size.takeIf { it > 0 }?.let {
                taffyPlural(R.plurals.taffy_filtering_exceptions_count, it, it)
            },
            modifier = Modifier.testTag(FILTERING_EXCEPTIONS_TEST_TAG),
        )
    }
    if (state.exceptionHosts.isEmpty()) {
        item(key = "filtering-no-exceptions", contentType = "status") {
            TaffyInfoTile(testTag = FILTERING_NO_EXCEPTIONS_TEST_TAG) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                ) {
                    Text(
                        text = taffyString(
                            if (state.enabled) {
                                R.string.taffy_filtering_no_exceptions
                            } else {
                                R.string.taffy_filtering_no_exceptions_off
                            },
                        ),
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textSecondary,
                    )
                    // There is no add-from-settings path, so the screen says
                    // where one comes from. The menu, not the shield badge:
                    // the badge only appears once something has been blocked.
                    Text(
                        text = taffyString(R.string.taffy_filtering_no_exceptions_how),
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textSecondary,
                    )
                }
            }
        }
        return
    }
    if (!state.enabled) {
        item(key = "filtering-exceptions-off", contentType = "status") {
            TaffyInfoTile(testTag = FILTERING_EXCEPTIONS_OFF_TEST_TAG) {
                Text(
                    text = taffyString(R.string.taffy_filtering_exceptions_off),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
    taffyGroupedCardItems(
        values = state.exceptionHosts,
        key = { it },
        contentType = { "exception" },
        testTag = { "$FILTERING_EXCEPTION_TEST_TAG_PREFIX$it" },
    ) { host ->
        ExceptionRow(host = host, state = state, onIntent = onIntent)
    }
}

/**
 * One allowed site and the way back.
 *
 * The description sits on the button rather than on a merged row. A
 * `semantics(mergeDescendants = true)` on the column collapsed the subtree into
 * one node whose `OnClick` was the row's, not the button's, so a click landed
 * on the text and no test could reach the control it was named after. It was
 * also wrong for a screen reader, which was told about a button while being
 * handed a node that is not one.
 */
@Composable
private fun ExceptionRow(
    host: String,
    state: FilteringSettingsUiState,
    onIntent: (FilteringSettingsIntent) -> Unit,
) {
    val removing = state.removal.host == host &&
        state.removal.status == FilteringSettingsUiState.Removal.Status.RUNNING
    val failed = state.removal.host == host &&
        state.removal.status == FilteringSettingsUiState.Removal.Status.FAILED
    val description = taffyString(
        if (state.enabled) {
            R.string.taffy_filtering_remove_exception
        } else {
            R.string.taffy_filtering_remove_exception_off
        },
        host,
    )
    Column(
        modifier = Modifier.padding(TaffyTheme.spacing.screenMargin),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = host,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
        if (removing || failed) {
            Text(
                text = taffyString(
                    if (removing) {
                        R.string.taffy_filtering_removing
                    } else {
                        R.string.taffy_filtering_remove_failed
                    },
                    host,
                ),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier
                    .testTag("$FILTERING_EXCEPTION_STATUS_TEST_TAG_PREFIX$host"),
            )
        }
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_filtering_remove),
            onClick = { onIntent(FilteringSettingsIntent.RemoveException(host)) },
            enabled = state.enabled,
            loading = removing,
            testTag = "$FILTERING_EXCEPTION_REMOVE_TEST_TAG_PREFIX$host",
            modifier = Modifier.semantics { contentDescription = description },
        )
    }
}

/** The tags screen SCR-206's semantics tests name. */
const val FILTERING_TOGGLE_TEST_TAG: String = "filtering_toggle"
const val FILTERING_SWITCH_TEST_TAG: String = "filtering_switch"
const val FILTERING_TOTAL_TEST_TAG: String = "filtering_total"
const val FILTERING_EXCEPTIONS_TEST_TAG: String = "filtering_exceptions"
const val FILTERING_LIST_TEST_TAG: String = "filtering_list"
const val FILTERING_EXCEPTION_TEST_TAG_PREFIX: String = "filtering_exception_"
const val FILTERING_EXCEPTION_REMOVE_TEST_TAG_PREFIX: String = "filtering_exception_remove_"
const val FILTERING_EXCEPTION_STATUS_TEST_TAG_PREFIX: String = "filtering_exception_status_"
const val FILTERING_NO_EXCEPTIONS_TEST_TAG: String = "filtering_no_exceptions"
const val FILTERING_EXCEPTIONS_OFF_TEST_TAG: String = "filtering_exceptions_off"
