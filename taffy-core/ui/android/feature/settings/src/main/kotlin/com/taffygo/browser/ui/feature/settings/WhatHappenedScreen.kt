// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
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
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffySummaryCard
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle

/** Screen SCR-412 — What happened. */
@Composable
fun WhatHappenedScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: WhatHappenedViewModel = screenViewModel(TaffyDestination.WhatHappened)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    WhatHappenedContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun WhatHappenedContent(
    state: WhatHappenedUiState,
    onIntent: (WhatHappenedIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyLazyScreen(
        destination = TaffyDestination.WhatHappened,
        title = taffyString(R.string.taffy_settings_what_happened_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = Modifier.testTag(WHAT_LIST_TEST_TAG),
    ) {
        item(key = "intro") {
            Text(
                text = taffyString(R.string.taffy_happened_intro),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        when (state.availability) {
            YouSurfaceAvailability.LOADING -> item(key = "loading") { WhatHappenedLoading() }
            YouSurfaceAvailability.UNAVAILABLE -> item(key = "unavailable") {
                WhatHappenedUnavailableCard()
            }
            YouSurfaceAvailability.READY -> whatHappenedReady(state, onIntent)
        }
    }
}

private fun LazyListScope.whatHappenedReady(
    state: WhatHappenedUiState,
    onIntent: (WhatHappenedIntent) -> Unit,
) {
    val blockedRequests = state.blockedRequestsThisWeek
    if (blockedRequests != null) {
        item(key = "summary") {
            TaffySummaryCard(
                value = taffyPlural(
                    R.plurals.taffy_happened_requests_blocked,
                    blockedRequests.coerceIn(0L, Int.MAX_VALUE.toLong()).toInt(),
                    blockedRequests,
                ),
                caption = taffyString(R.string.taffy_time_this_week),
                positiveTone = blockedRequests > 0L,
                testTag = WHAT_SUMMARY_TEST_TAG,
            )
        }
    }
    if (state.days.isEmpty()) {
        item(key = "empty") { WhatHappenedEmptyCard() }
        return
    }
    val today = LocalDate.now().toEpochDay()
    state.days.forEach { day ->
        item(key = "day-${day.epochDay}") {
            TaffySectionHeader(title = whatHappenedDayTitle(day.epochDay, today))
        }
        items(
            items = day.events,
            key = { event -> "event-${event.id}" },
        ) { event ->
            TaffyGroupedCard {
                WhatHappenedEventRow(event, onIntent)
            }
        }
    }
}

@Composable
private fun WhatHappenedEventRow(
    event: WhatHappenedRepository.Event,
    onIntent: (WhatHappenedIntent) -> Unit,
) {
    val title = whatHappenedTitle(event)
    val supporting = whatHappenedSupporting(event)
    val description = taffyString(R.string.taffy_happened_row_description, title, supporting)
    val open = event.workspaceId?.takeIf { !event.workspaceGone }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(
                if (open != null) {
                    Modifier.clickable { onIntent(WhatHappenedIntent.OpenTask(open)) }
                } else {
                    Modifier
                },
            )
            .heightIn(min = EventRowMinHeight)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            )
            .testTag("$WHAT_EVENT_TEST_TAG_PREFIX${event.id}")
            .semantics(mergeDescendants = true) { contentDescription = description },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SettingsGlyph(whatHappenedIcon(event.kind))
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = supporting,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

@Composable
private fun WhatHappenedLoading() {
    TaffySkeleton(
        modifier = Modifier
            .fillMaxWidth()
            .height(88.dp),
        accessibleDescription = taffyString(R.string.taffy_happened_loading),
    )
    TaffyGroupedCard {
        repeat(4) { index ->
            TaffySkeleton(
                modifier = Modifier
                    .padding(TaffyTheme.spacing.screenMargin)
                    .fillMaxWidth()
                    .height(16.dp),
            )
            if (index < 3) TaffyGroupedCardDivider()
        }
    }
}

@Composable
internal fun whatHappenedTitle(event: WhatHappenedRepository.Event): String = when (event.kind) {
    WhatHappenedRepository.Kind.WORKSPACE_RESULT ->
        taffyPlural(
            R.plurals.taffy_happened_workspace_sources,
            event.sourceCount,
            event.sourceCount,
        )
    WhatHappenedRepository.Kind.TASK_STOPPED -> taffyString(R.string.taffy_happened_stopped)
    WhatHappenedRepository.Kind.TASK_FAILED -> taffyString(R.string.taffy_happened_failed)
}

@Composable
private fun whatHappenedSupporting(event: WhatHappenedRepository.Event): String {
    val time = Instant.ofEpochMilli(event.epochMillis)
        .atZone(ZoneId.systemDefault())
        .toLocalTime()
        .format(WHAT_TIME_FORMATTER)
    val updated = taffyString(R.string.taffy_happened_updated, time)
    return if (event.workspaceGone) {
        "$updated · ${taffyString(R.string.taffy_happened_workspace_deleted)}"
    } else {
        updated
    }
}

@Composable
private fun whatHappenedDayTitle(epochDay: Long, todayEpochDay: Long): String = when (epochDay) {
    todayEpochDay -> taffyString(R.string.taffy_happened_today)
    todayEpochDay - 1L -> taffyString(R.string.taffy_happened_yesterday)
    else -> LocalDate.ofEpochDay(epochDay).format(WHAT_DATE_FORMATTER)
}

private fun whatHappenedIcon(kind: WhatHappenedRepository.Kind) = when (kind) {
    WhatHappenedRepository.Kind.WORKSPACE_RESULT,
    WhatHappenedRepository.Kind.TASK_STOPPED,
    WhatHappenedRepository.Kind.TASK_FAILED,
    -> TaffyIcon.Newspaper
}

/** Tags screen SCR-412's semantics tests name. */
const val WHAT_SUMMARY_TEST_TAG: String = "what_happened_summary"
const val WHAT_LIST_TEST_TAG: String = "what_happened_list"
const val WHAT_EVENT_TEST_TAG_PREFIX: String = "what_happened_event_"
const val WHAT_UNAVAILABLE_TEST_TAG: String = "what_happened_unavailable"
const val WHAT_EMPTY_TEST_TAG: String = "what_happened_empty"

private val EventRowMinHeight = 56.dp
private val WHAT_TIME_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm")
private val WHAT_DATE_FORMATTER: DateTimeFormatter =
    DateTimeFormatter.ofLocalizedDate(FormatStyle.MEDIUM)
