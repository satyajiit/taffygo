// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyIconButton
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle

/**
 * Screen SCR-201 — pages opened on this phone.
 *
 * Private tabs never appear. Pages Taffy opened for a task are not History.
 * Clear opens screen SCR-207 rather than wiping a range from this list.
 */
@Composable
fun HistoryScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: HistoryViewModel = screenViewModel(TaffyDestination.History)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    HistoryContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = { viewModel.onIntent(HistoryIntent.Dismiss, navigator) },
        modifier = modifier,
    )
}

/** The stateless half, which is what a preview and a semantics test render. */
@Composable
fun HistoryContent(
    state: HistoryUiState,
    onIntent: (HistoryIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    var pendingDelete by remember { mutableStateOf<HistoryVisit?>(null) }

    TaffyScreen(
        destination = TaffyDestination.History,
        title = taffyString(R.string.taffy_pages_history_title),
        onBack = onBack,
        modifier = modifier,
        scrollable = state.days.isEmpty(),
        header = {
            HistoryHeader(state = state, onIntent = onIntent)
        },
    ) {
        when {
            state.isLoading -> PagesRecordSkeleton(
                rows = HISTORY_SKELETON_ROWS,
                firstDescription = taffyString(R.string.taffy_pages_history_loading),
                rowHeight = HISTORY_ROW_HEIGHT,
                testTag = HISTORY_LOADING_TEST_TAG,
            )
            state.isUnavailable -> TaffyEmptyState(
                title = taffyString(R.string.taffy_pages_history_unavailable_title),
                body = taffyString(R.string.taffy_pages_history_unavailable_body),
                leading = { HistoryEmptyGlyph() },
            )
            state.isEmpty -> TaffyEmptyState(
                title = taffyString(R.string.taffy_pages_history_empty_title),
                body = taffyString(R.string.taffy_pages_history_empty_body),
                leading = { HistoryEmptyGlyph() },
            )
            state.hasNoMatches -> TaffyEmptyState(
                title = taffyString(R.string.taffy_pages_history_no_matches_title),
                body = taffyString(R.string.taffy_pages_history_no_matches_body),
            )
            else -> HistoryDays(
                state = state,
                onIntent = onIntent,
                onAskDelete = { pendingDelete = it },
            )
        }
    }

    pendingDelete?.let { visit ->
        val title = visit.title.ifBlank { visit.host }
        PagesConfirmSheet(
            title = taffyString(R.string.taffy_pages_history_delete_confirm_title),
            body = taffyString(R.string.taffy_pages_history_delete_confirm_body, title, visit.host),
            confirmLabel = taffyString(R.string.taffy_pages_remove),
            cancelLabel = taffyString(R.string.taffy_pages_cancel),
            onConfirm = {
                onIntent(HistoryIntent.Delete(visit.id))
                pendingDelete = null
            },
            onDismiss = { pendingDelete = null },
            testTag = HISTORY_CONFIRM_TEST_TAG,
        )
    }
}

@Composable
private fun HistoryHeader(
    state: HistoryUiState,
    onIntent: (HistoryIntent) -> Unit,
) {
    TaffySearchField(
        value = state.query,
        onValueChange = { onIntent(HistoryIntent.QueryChanged(it)) },
        placeholder = taffyString(R.string.taffy_pages_history_search),
        testTag = HISTORY_SEARCH_TEST_TAG,
    )
    TaffySecondaryButton(
        label = taffyString(R.string.taffy_pages_history_clear),
        onClick = { onIntent(HistoryIntent.Clear) },
        testTag = HISTORY_CLEAR_TEST_TAG,
    )
    if (!state.isEmpty && !state.isUnavailable) {
        Text(
            text = taffyString(R.string.taffy_pages_history_caption),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(HISTORY_CAPTION_TEST_TAG),
        )
    }
}

@Composable
private fun HistoryDays(
    state: HistoryUiState,
    onIntent: (HistoryIntent) -> Unit,
    onAskDelete: (HistoryVisit) -> Unit,
) {
    val locale = LocalConfiguration.current.locales[0]
    val zone = ZoneId.systemDefault()
    val dateFormatter = remember(locale) {
        DateTimeFormatter.ofLocalizedDate(FormatStyle.MEDIUM).withLocale(locale)
    }
    val timeFormatter = remember(locale, zone) {
        DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT)
            .withLocale(locale)
            .withZone(zone)
    }
    LazyColumn(
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        modifier = Modifier
            .fillMaxSize()
            .testTag(HISTORY_LIST_TEST_TAG),
    ) {
        state.days.forEach { day ->
            item(key = "day-${day.epochDay}", contentType = "day") {
                TaffySectionHeader(
                    title = historyDayLabel(day, dateFormatter),
                    modifier = Modifier.testTag(
                        "$HISTORY_DAY_TEST_TAG_PREFIX${day.epochDay}",
                    ),
                )
            }
            items(
                items = day.visits,
                key = { it.id.value },
                contentType = { "visit" },
            ) { visit ->
                TaffyGroupedCard {
                    HistoryVisitRow(
                        visit = visit,
                        timeFormatter = timeFormatter,
                        mark = state.siteMarks[visit.host],
                        canOpen = state.canOpen,
                        onOpen = { onIntent(HistoryIntent.Open(visit.id)) },
                        onAskDelete = { onAskDelete(visit) },
                    )
                }
            }
        }
    }
}

@Composable
private fun HistoryVisitRow(
    visit: HistoryVisit,
    timeFormatter: DateTimeFormatter,
    mark: android.graphics.Bitmap?,
    canOpen: Boolean,
    onOpen: () -> Unit,
    onAskDelete: () -> Unit,
) {
    val title = visit.title.ifBlank { visit.host }
    val time = historyVisitTime(visit.visitedAtEpochMillis, timeFormatter)
    val supporting = taffyString(R.string.taffy_pages_history_supporting, visit.host, time)
    val description = taffyString(
        R.string.taffy_pages_history_row_description,
        title,
        visit.host,
        time,
    )
    PagesSwipeDelete(enabled = true, onSwiped = onAskDelete) {
        PagesRecordRow(
            title = title,
            supporting = supporting,
            accessibleDescription = description,
            testTag = "$HISTORY_VISIT_TEST_TAG_PREFIX${visit.id.value}",
            minHeight = HISTORY_ROW_HEIGHT,
            onClick = onOpen.takeIf { canOpen },
            leading = { PagesSiteMark(host = visit.host, mark = mark) },
            trailing = {
                TaffyIconButton(
                    icon = TaffyIcon.Trash,
                    contentDescription = taffyString(R.string.taffy_pages_history_delete),
                    onClick = onAskDelete,
                    testTag = "$HISTORY_DELETE_TEST_TAG_PREFIX${visit.id.value}",
                )
            },
        )
    }
}

@Composable
private fun HistoryEmptyGlyph() {
    Icon(
        imageVector = TaffyIcon.ClockCounterClockwise,
        contentDescription = null,
        tint = TaffyTheme.colors.textSecondary,
    )
}

@Composable
private fun historyDayLabel(day: HistoryDay, dateFormatter: DateTimeFormatter): String =
    when (day.kind) {
        HistoryDay.Kind.TODAY -> taffyString(R.string.taffy_pages_history_today)
        HistoryDay.Kind.YESTERDAY -> taffyString(R.string.taffy_pages_history_yesterday)
        HistoryDay.Kind.DATE -> dateFormatter.format(LocalDate.ofEpochDay(day.epochDay))
    }

private fun historyVisitTime(
    visitedAtEpochMillis: Long,
    formatter: DateTimeFormatter,
): String = formatter.format(Instant.ofEpochMilli(visitedAtEpochMillis))

/** The tags screen SCR-201's semantics tests name. */
const val HISTORY_SEARCH_TEST_TAG: String = "history_search"
const val HISTORY_CLEAR_TEST_TAG: String = "history_clear"
const val HISTORY_CAPTION_TEST_TAG: String = "history_caption"
const val HISTORY_LIST_TEST_TAG: String = "history_list"
const val HISTORY_LOADING_TEST_TAG: String = "history_loading"
const val HISTORY_CONFIRM_TEST_TAG: String = "history_delete_confirm"
const val HISTORY_DAY_TEST_TAG_PREFIX: String = "history_day_"
const val HISTORY_VISIT_TEST_TAG_PREFIX: String = "history_visit_"
const val HISTORY_DELETE_TEST_TAG_PREFIX: String = "history_delete_"

private const val HISTORY_SKELETON_ROWS = 5
private val HISTORY_ROW_HEIGHT = 56.dp
