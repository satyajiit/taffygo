// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.text.format.DateUtils
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-413 — saved sign-in metadata. */
@Composable
fun SavedSignInsScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: SavedSignInsViewModel = screenViewModel(TaffyDestination.SavedSignIns)
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.onShown() }
    SavedSignInsContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = {
            if (state.opened != null) {
                viewModel.onIntent(SavedSignInsIntent.DismissDetail)
            } else if (showUp) {
                navigator.goBack()
            }
        },
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun SavedSignInsContent(
    state: SavedSignInsUiState,
    onIntent: (SavedSignInsIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val hasRows = state.opened == null &&
        state.availability == YouSurfaceAvailability.READY &&
        state.matching.isNotEmpty()
    TaffyLazyScreen(
        destination = TaffyDestination.SavedSignIns,
        title = taffyString(R.string.taffy_settings_saved_sign_ins_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = if (hasRows) Modifier.testTag(SIGN_INS_LIST_TEST_TAG) else Modifier,
    ) {
        item(key = "sign-ins-promise", contentType = "promise") {
            Text(
                text = taffyString(R.string.taffy_saved_never_sends),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(SIGN_INS_PROMISE_TEST_TAG),
            )
        }
        if (state.opened != null) {
            item(key = "sign-in-detail", contentType = "detail") {
                SavedSignInDetail(state = state, onIntent = onIntent)
            }
        } else {
            when (state.availability) {
                YouSurfaceAvailability.LOADING -> item(
                    key = "sign-ins-loading",
                    contentType = "status",
                ) { SavedSignInsLoading() }
                YouSurfaceAvailability.UNAVAILABLE -> item(
                    key = "sign-ins-unavailable",
                    contentType = "status",
                ) {
                    TaffyEmptyState(
                        title = taffyString(R.string.taffy_sign_ins_unavailable_title),
                        body = taffyString(R.string.taffy_sign_ins_unavailable_body),
                        leading = { SavedSignInsGlyph() },
                    )
                }
                YouSurfaceAvailability.READY -> savedSignInsReady(state, onIntent)
            }
        }
    }
}

private fun LazyListScope.savedSignInsReady(
    state: SavedSignInsUiState,
    onIntent: (SavedSignInsIntent) -> Unit,
) {
    if (state.records.isNotEmpty()) {
        item(key = "sign-ins-search", contentType = "search") {
            TaffySearchField(
                value = state.query,
                onValueChange = { onIntent(SavedSignInsIntent.QueryChanged(it)) },
                placeholder = taffyString(R.string.taffy_sign_ins_search),
                testTag = SIGN_INS_SEARCH_TEST_TAG,
            )
        }
    }
    if (state.matching.isEmpty()) {
        item(key = "sign-ins-empty", contentType = "status") {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_sign_ins_empty_title),
                body = taffyString(R.string.taffy_sign_ins_empty_body),
                leading = { SavedSignInsGlyph() },
            )
        }
        return
    }
    items(
        items = state.matching,
        key = SavedSignInsRepository.Record::id,
        contentType = { "sign-in" },
    ) { record ->
        TaffyGroupedCard {
            val description = taffyString(
                R.string.taffy_sign_ins_row_description,
                record.site,
                record.username,
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { onIntent(SavedSignInsIntent.Open(record.id)) }
                    .heightIn(min = SignInRowMinHeight)
                    .padding(
                        horizontal = TaffyTheme.spacing.screenMargin,
                        vertical = TaffyTheme.spacing.snug,
                    )
                    .testTag("$SIGN_INS_ROW_TEST_TAG_PREFIX${record.id}")
                    .semantics(mergeDescendants = true) { contentDescription = description },
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                SettingsGlyph(TaffyIcon.Password)
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
                ) {
                    Text(record.site, style = TaffyTheme.typography.body)
                    Text(
                        record.username,
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textSecondary,
                    )
                    if (record.lastUsedEpochMillis > 0L) {
                        Text(
                            text = taffyString(
                                R.string.taffy_sign_ins_last_used,
                                DateUtils.getRelativeTimeSpanString(record.lastUsedEpochMillis),
                            ),
                            style = TaffyTheme.typography.caption,
                            color = TaffyTheme.colors.textSecondary,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun SavedSignInsLoading() {
    TaffyGroupedCard {
        repeat(4) { index ->
            TaffySkeleton(
                modifier = Modifier
                    .padding(TaffyTheme.spacing.screenMargin)
                    .fillMaxWidth()
                    .height(16.dp),
                accessibleDescription = if (index == 0) {
                    taffyString(R.string.taffy_sign_ins_loading)
                } else {
                    null
                },
            )
            if (index < 3) TaffyGroupedCardDivider()
        }
    }
}

@Composable
private fun SavedSignInsGlyph() {
    Icon(
        imageVector = TaffyIcon.Password,
        contentDescription = null,
        tint = TaffyTheme.colors.textPrimary,
        modifier = Modifier.size(SettingsGlyphSize),
    )
}

const val SIGN_INS_PROMISE_TEST_TAG: String = "saved_sign_ins_promise"
const val SIGN_INS_SEARCH_TEST_TAG: String = "saved_sign_ins_search"
const val SIGN_INS_LIST_TEST_TAG: String = "saved_sign_ins_list"
const val SIGN_INS_ROW_TEST_TAG_PREFIX: String = "saved_sign_ins_row_"

private val SignInRowMinHeight = 56.dp
