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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
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
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString
import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle

/** Screen SCR-505 — Memory. */
@Composable
fun MemoryScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: MemoryViewModel = screenViewModel(TaffyDestination.Memory)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    MemoryContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun MemoryContent(
    state: MemoryUiState,
    onIntent: (MemoryIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyLazyScreen(
        destination = TaffyDestination.Memory,
        title = taffyString(R.string.taffy_settings_memory_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = Modifier.testTag(MEMORY_LIST_TEST_TAG),
        header = {
            Text(
                text = taffyString(R.string.taffy_memory_promise),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(MEMORY_PROMISE_TEST_TAG),
            )
            if (state.processScoped) {
                Text(
                    text = taffyString(R.string.taffy_memory_process_caption),
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            if (state.showSearch) {
                TaffySearchField(
                    value = state.query,
                    onValueChange = { onIntent(MemoryIntent.QueryChanged(it)) },
                    placeholder = taffyString(R.string.taffy_memory_search),
                    testTag = MEMORY_SEARCH_TEST_TAG,
                )
            }
        },
        footer = if (state.availability == YouSurfaceAvailability.READY) {
            {
                TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_memory_add),
                    onClick = { onIntent(MemoryIntent.Add) },
                    modifier = Modifier.fillMaxWidth(),
                    testTag = MEMORY_ADD_TEST_TAG,
                )
            }
        } else {
            null
        },
    ) {
        when (state.availability) {
            YouSurfaceAvailability.LOADING -> item(contentType = "loading") { MemoryLoading() }
            YouSurfaceAvailability.UNAVAILABLE -> item(contentType = "unavailable") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_memory_unavailable_title),
                    body = taffyString(R.string.taffy_memory_unavailable_body),
                    leading = { MemoryGlyph() },
                )
            }
            YouSurfaceAvailability.READY -> memoryReadyItems(state, onIntent)
        }
    }
    if (state.editor != null) MemoryEditorSheet(state = state, onIntent = onIntent)
}

private fun LazyListScope.memoryReadyItems(
    state: MemoryUiState,
    onIntent: (MemoryIntent) -> Unit,
) {
    val visibleNotes = state.visibleNotes
    if (visibleNotes.isEmpty) {
        item(contentType = "empty") {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_memory_empty_title),
                body = taffyString(R.string.taffy_memory_empty_body),
                leading = { MemoryGlyph() },
                action = {
                    TaffyPrimaryButton(
                        label = taffyString(R.string.taffy_memory_add),
                        onClick = { onIntent(MemoryIntent.Add) },
                        testTag = MEMORY_EMPTY_ADD_TEST_TAG,
                    )
                },
            )
        }
        return
    }
    if (visibleNotes.youWrote.isNotEmpty()) {
        memoryGroup(
            titleResource = R.string.taffy_memory_you_wrote,
            count = visibleNotes.youWrote.size,
            notes = visibleNotes.youWrote,
            youWrote = true,
            onOpen = { onIntent(MemoryIntent.Open(it)) },
        )
    }
    if (visibleNotes.taffyNoticed.isNotEmpty()) {
        memoryGroup(
            titleResource = R.string.taffy_memory_taffy_noticed,
            count = visibleNotes.taffyNoticed.size,
            notes = visibleNotes.taffyNoticed,
            youWrote = false,
            onOpen = { onIntent(MemoryIntent.Open(it)) },
        )
    }
}

private fun LazyListScope.memoryGroup(
    titleResource: Int,
    count: Int,
    notes: List<MemoryRepository.Note>,
    youWrote: Boolean,
    onOpen: (String) -> Unit,
) {
    item(key = "heading-$youWrote", contentType = "heading") {
        val title = taffyString(titleResource)
        val heading = taffyString(
            R.string.taffy_memory_group_heading,
            title,
            taffyPlural(R.plurals.taffy_memory_group_count, count, count),
        )
        TaffySectionHeader(
            title = title,
            modifier = Modifier.semantics { contentDescription = heading },
        )
    }
    items(
        count = notes.size,
        key = { index -> notes[index].id },
        contentType = { "memory" },
    ) { index ->
        TaffyGroupedCard(
            testTag = if (index == 0) {
                if (youWrote) MEMORY_YOU_WROTE_TEST_TAG else MEMORY_TAFFY_NOTICED_TEST_TAG
            } else {
                null
            },
        ) {
            MemoryRow(notes[index], youWrote, onOpen)
        }
    }
}

@Composable
private fun MemoryRow(
    note: MemoryRepository.Note,
    youWrote: Boolean,
    onOpen: (String) -> Unit,
) {
    val why = memoryWhyLine(note)
    val description = taffyString(R.string.taffy_memory_row_description, note.statement, why)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onOpen(note.id) }
            .heightIn(min = MemoryRowMinHeight)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            )
            .testTag("$MEMORY_ROW_TEST_TAG_PREFIX${note.id}")
            .semantics(mergeDescendants = true) { contentDescription = description },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SettingsGlyph(if (youWrote) TaffyIcon.PencilSimple else TaffyIcon.Sparkle)
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = note.statement,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = why,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

@Composable
private fun MemoryLoading() {
    TaffyGroupedCard {
        repeat(2) { index ->
            TaffySkeleton(
                modifier = Modifier
                    .padding(TaffyTheme.spacing.screenMargin)
                    .fillMaxWidth()
                    .height(20.dp),
                accessibleDescription = if (index == 0) {
                    taffyString(R.string.taffy_memory_loading)
                } else {
                    null
                },
            )
            if (index == 0) TaffyGroupedCardDivider()
        }
    }
}

@Composable
private fun MemoryGlyph() {
    Icon(
        imageVector = TaffyIcon.Sparkle,
        contentDescription = null,
        tint = TaffyTheme.colors.textPrimary,
        modifier = Modifier.size(SettingsGlyphSize),
    )
}

@Composable
internal fun memoryWhyLine(note: MemoryRepository.Note): String {
    val added = LocalDate.ofEpochDay(note.addedEpochDay)
        .format(DateTimeFormatter.ofLocalizedDate(FormatStyle.MEDIUM))
    val why = when (note.source) {
        MemoryRepository.Source.YOU_WROTE ->
            taffyString(R.string.taffy_memory_why_added, added)
        MemoryRepository.Source.TAFFY_NOTICED -> taffyString(
            R.string.taffy_memory_why_yes,
            note.workspaceName?.takeIf { it.isNotBlank() } ?: added,
        )
    }
    val scope = when (note.scope) {
        MemoryRepository.Scope.ALL_TASKS -> taffyString(R.string.taffy_memory_scope_all)
        MemoryRepository.Scope.WORKSPACE -> taffyString(
            R.string.taffy_memory_scope_workspace,
            note.scopeWorkspaceName.orEmpty(),
        )
    }
    var detail = taffyString(R.string.taffy_happened_summary, why, scope)
    if (note.sensitive) {
        detail = taffyString(
            R.string.taffy_happened_summary,
            detail,
            taffyString(R.string.taffy_memory_sensitive),
        )
    }
    val until = note.expiresEpochDay ?: return detail
    val expiry = LocalDate.ofEpochDay(until)
        .format(DateTimeFormatter.ofLocalizedDate(FormatStyle.MEDIUM))
    return taffyString(
        R.string.taffy_happened_summary,
        detail,
        taffyString(R.string.taffy_memory_until, expiry),
    )
}

const val MEMORY_PROMISE_TEST_TAG: String = "memory_promise"
const val MEMORY_SEARCH_TEST_TAG: String = "memory_search"
const val MEMORY_ADD_TEST_TAG: String = "memory_add"
const val MEMORY_EMPTY_ADD_TEST_TAG: String = "memory_empty_add"
const val MEMORY_YOU_WROTE_TEST_TAG: String = "memory_you_wrote"
const val MEMORY_TAFFY_NOTICED_TEST_TAG: String = "memory_taffy_noticed"
const val MEMORY_ROW_TEST_TAG_PREFIX: String = "memory_row_"
const val MEMORY_LIST_TEST_TAG: String = "memory_list"

private val MemoryRowMinHeight = 72.dp
