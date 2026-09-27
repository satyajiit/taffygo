// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.rememberScreenViewModelStoreOwner
import com.taffygo.browser.ui.core.ui.taffyString
import com.taffygo.browser.ui.core.ui.taffyPlural

/**
 * Add pages as a local sheet on Ask. Not a destination.
 */
@Composable
fun AttachPagesSheet(
    destination: TaffyDestination,
    alreadyAttached: List<TabId>,
    onConfirm: (List<TabId>) -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val viewModel: AttachPagesViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(destination),
        key = "${destination.route}/attach-pages",
    )
    LaunchedEffect(Unit) { viewModel.start(alreadyAttached) }
    val state by viewModel.state.collectAsStateWithLifecycle()
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_attach_pages_title),
        onDismissRequest = onDismiss,
        modifier = modifier,
        testTag = ATTACH_PAGES_SHEET_TEST_TAG,
    ) {
        AttachPagesContent(
            state = state,
            onIntent = { intent ->
                when (intent) {
                    AttachPagesIntent.Confirm -> {
                        onConfirm(state.tickedIds.toList())
                        onDismiss()
                    }
                    AttachPagesIntent.Dismiss -> onDismiss()
                    else -> viewModel.onIntent(intent)
                }
            },
        )
    }
}

/** The stateless half. */
@Composable
fun AttachPagesContent(
    state: AttachPagesUiState,
    onIntent: (AttachPagesIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.fillMaxWidth().heightIn(max = 560.dp),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Text(
            text = taffyString(R.string.taffy_attach_pages_body),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        when (state.status) {
            AskPagesSnapshot.Status.LOADING -> AttachPagesLoading()
            AskPagesSnapshot.Status.UNAVAILABLE -> TaffyEmptyState(
                title = taffyString(R.string.taffy_attach_pages_unavailable_title),
                body = taffyString(R.string.taffy_attach_pages_unavailable_body),
            )
            AskPagesSnapshot.Status.READY -> AttachPagesReady(state, onIntent)
        }
    }
}

@Composable
private fun AttachPagesLoading() {
    val loading = taffyString(R.string.taffy_attach_pages_loading)
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        repeat(LOADING_ROW_COUNT) { index ->
            TaffySkeleton(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(AttachRowHeight),
                shape = TaffyTheme.shapes.row,
                accessibleDescription = if (index == 0) loading else null,
            )
        }
    }
}

@Composable
private fun ColumnScope.AttachPagesReady(
    state: AttachPagesUiState,
    onIntent: (AttachPagesIntent) -> Unit,
) {
    TaffySearchField(
        value = state.query,
        onValueChange = { onIntent(AttachPagesIntent.SearchChanged(it)) },
        placeholder = taffyString(R.string.taffy_attach_pages_search),
        testTag = ATTACH_PAGES_SEARCH_TEST_TAG,
    )
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffySectionHeader(
            title = taffyString(R.string.taffy_attach_pages_your_tabs),
            modifier = Modifier.weight(1f),
        )
        Text(
            text = taffyPlural(R.plurals.taffy_attach_pages_selected, state.tickedIds.size, state.tickedIds.size),
            style = TaffyTheme.typography.caption,
            color = TaffyTheme.colors.textSecondary,
        )
    }
    val visible = state.visibleRows
    if (visible.isEmpty()) {
        Text(
            text = taffyString(R.string.taffy_attach_pages_empty_search),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(ATTACH_PAGES_EMPTY_SEARCH_TEST_TAG),
        )
    } else {
        LazyColumn(
            modifier = Modifier
                .weight(1f, fill = false)
                .heightIn(max = AttachListMaxHeight)
                .testTag(ATTACH_PAGES_LIST_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            items(
                items = visible,
                key = { row -> row.tabId.value },
            ) { row ->
                AttachPagesRow(
                    row = row,
                    onToggle = { onIntent(AttachPagesIntent.Toggle(row.tabId)) },
                )
            }
        }
    }
    if (state.rows.isEmpty() && state.taffyTabsPresent) {
        Text(
            text = taffyString(R.string.taffy_attach_pages_taffy_tabs),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(ATTACH_PAGES_TAFFY_TABS_TEST_TAG),
        )
    }
    if (state.cautionOverHandful) {
        Text(
            text = taffyString(R.string.taffy_attach_pages_caution),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.caution,
            modifier = Modifier.testTag(ATTACH_PAGES_CAUTION_TEST_TAG),
        )
    }
    TaffyPrimaryButton(
        label = taffyString(R.string.taffy_attach_pages_use),
        onClick = { onIntent(AttachPagesIntent.Confirm) },
        testTag = ATTACH_PAGES_CONFIRM_TEST_TAG,
    )
}

@Composable
private fun AttachPagesRow(
    row: AttachPagesUiState.Row,
    onToggle: () -> Unit,
) {
    val spoken = taffyString(
        if (row.ticked) {
            R.string.taffy_attach_pages_row_chosen
        } else {
            R.string.taffy_attach_pages_row
        },
        row.title,
        row.host,
    )
    val colors = TaffyTheme.colors
    val shape = TaffyTheme.shapes.card
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = AttachRowHeight)
            .clip(shape)
            .background(if (row.ticked) colors.ribbonOneWash else colors.surfaceRaised)
            .border(TaffyBorders.standard, if (row.ticked) colors.accent else colors.outline, shape)
            .clickable(role = Role.Checkbox, onClick = onToggle)
            .padding(TaffyTheme.spacing.snug)
            .testTag("$ATTACH_PAGES_ROW_TEST_TAG_PREFIX${row.tabId.value}")
            .semantics(mergeDescendants = true) {
                contentDescription = spoken
                selected = row.ticked
            },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        TaffyGlyphFrame(
            size = 40.dp,
            color = colors.surfaceRaised,
            modifier = Modifier.testTag("$ATTACH_PAGES_ICON_TEST_TAG_PREFIX${row.tabId.value}"),
        ) {
            val favicon = row.favicon
            if (favicon != null) {
                Image(
                    bitmap = favicon.asImageBitmap(),
                    contentDescription = null,
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.size(24.dp),
                )
            } else {
                Icon(
                    imageVector = TaffyIcon.GlobeSimple,
                    contentDescription = null,
                    tint = colors.textSecondary,
                    modifier = Modifier.size(24.dp),
                )
            }
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = row.title,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = row.host,
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Box(
            modifier = Modifier.size(24.dp).clip(TaffyTheme.shapes.chip)
                .background(if (row.ticked) colors.textPrimary else colors.surfaceRaised)
                .border(TaffyBorders.standard, colors.textSecondary, TaffyTheme.shapes.chip),
            contentAlignment = Alignment.Center,
        ) {
            if (row.ticked) Icon(
                imageVector = TaffyIcon.Check,
                contentDescription = null,
                tint = colors.surface,
                modifier = Modifier.size(TickIconSize),
            )
        }
    }
}

private val AttachRowHeight = 72.dp
private val AttachListMaxHeight = 280.dp
private val TickIconSize = 20.dp
private const val LOADING_ROW_COUNT = 3

/** The tags the Add pages sheet's tests name. */
const val ATTACH_PAGES_SHEET_TEST_TAG: String = "attach_pages_sheet"
const val ATTACH_PAGES_LIST_TEST_TAG: String = "attach_pages_list"
const val ATTACH_PAGES_SEARCH_TEST_TAG: String = "attach_pages_search"
const val ATTACH_PAGES_CONFIRM_TEST_TAG: String = "attach_pages_confirm"
const val ATTACH_PAGES_CAUTION_TEST_TAG: String = "attach_pages_caution"
const val ATTACH_PAGES_EMPTY_SEARCH_TEST_TAG: String = "attach_pages_empty_search"
const val ATTACH_PAGES_TAFFY_TABS_TEST_TAG: String = "attach_pages_taffy_tabs"
const val ATTACH_PAGES_ROW_TEST_TAG_PREFIX: String = "attach_pages_row_"
const val ATTACH_PAGES_ICON_TEST_TAG_PREFIX: String = "attach_pages_icon_"
