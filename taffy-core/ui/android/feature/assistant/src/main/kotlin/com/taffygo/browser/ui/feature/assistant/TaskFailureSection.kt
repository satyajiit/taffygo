// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrameLargeSize
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyStatTile
import com.taffygo.browser.ui.core.ui.TaffyTileWash
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * What a failed task shows: what went wrong, how far it got, and what the
 * findings it did collect are.
 *
 * It used to be one grey sentence. "Couldn't finish — Taffy could not read
 * enough to answer" under a chip that says Failed, and then nothing — no shape,
 * no numbers, and no account of the pages Taffy had actually read before it
 * stopped. A person looking at it could not tell whether Taffy had done
 * nothing or had done almost everything, and the one control under it offered
 * to discard a "temporary workspace" the screen had never mentioned existed.
 *
 * So there are three parts now, and each answers a question the sentence left
 * open. **What happened** is the shape and the reason, on the danger wash the
 * status chip already uses, so the state is legible before a word is read.
 * **How far it got** is two counts — pages read, from what the task did, and
 * things kept, from its workspace — because "it could not read enough" means
 * something quite different beside 0 pages than beside 6. **What the workspace is** is one
 * sentence, and it is here rather than only inside the discard dialog: a
 * person should not first learn that a workspace exists from a button offering
 * to throw it away.
 *
 * [onOpenSetup] is the screen's navigation, handed in the way `onBack` is,
 * because the view model has no navigator; a caller with no way to open
 * AI & providers draws the reason and no offer. The actions stay in the fixed
 * results footer.
 */
@Composable
internal fun TaskFailureSection(
    state: TaskViewUiState,
    onOpenSetup: (() -> Unit)?,
) {
    val failure = state.failure
    if (state.state != TaskDisplayState.FAILED || failure == null) return
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        FailureCard(failure)
        FailureProgress(state)
        WorkspaceNote(state)
        if (state.offersSetup && onOpenSetup != null) {
            val refused = failure == TaskFailureReason.PROVIDER_REFUSED
            Column(Modifier.testTag(TASK_SETUP_TEST_TAG)) {
                TaffyEmptyState(
                    title = taffyString(
                        if (refused) {
                            R.string.taffy_task_view_failed_refused_title
                        } else {
                            R.string.taffy_task_view_failed_provider_title
                        },
                    ),
                    body = taffyString(
                        if (refused) {
                            R.string.taffy_task_view_failed_refused_body
                        } else {
                            R.string.taffy_task_view_failed_provider_body
                        },
                    ),
                    leading = {
                        Icon(
                            imageVector = TaffyIcon.Key,
                            contentDescription = null,
                            tint = TaffyTheme.colors.textSecondary,
                            modifier = Modifier.size(SetupGlyphSize),
                        )
                    },
                )
            }
        }
    }
}

/** The shape, the heading and the reason, on the state's own wash. */
@Composable
private fun FailureCard(failure: TaskFailureReason) {
    val colors = TaffyTheme.colors
    val shape = TaffyTheme.shapes.card
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(shape)
            .background(colors.dangerWash)
            .border(TaffyBorders.standard, colors.outline, shape)
            .padding(TaffyTheme.spacing.snug)
            .testTag(TASK_FAILURE_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.Top,
    ) {
        TaffyGlyphFrame(size = TaffyGlyphFrameLargeSize, color = colors.surface) {
            Icon(
                imageVector = taskFailureGlyph(failure),
                contentDescription = null,
                tint = colors.dangerText,
                modifier = Modifier.size(FailureGlyphSize),
            )
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = taffyString(taskFailureHeadline(failure)),
                style = TaffyTheme.typography.label,
                color = colors.dangerText,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                text = taffyString(taskFailureLine(failure)),
                style = TaffyTheme.typography.detail,
                color = colors.textSecondary,
            )
        }
    }
}

/**
 * How far it got, in two numbers the task actually holds.
 *
 * Pages read come from the timeline, as the bar's do, and not from the
 * workspace's sources, which are the pages consented to at the start whether
 * or not any was read. Drawn even at zero, because zero is the answer to the
 * question and hiding it turns "Taffy read nothing" into "Taffy read
 * something, unstated".
 */
@Composable
private fun FailureProgress(state: TaskViewUiState) {
    Row(
        modifier = Modifier.fillMaxWidth().testTag(TASK_FAILURE_PROGRESS_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        // The number is the tile's own value and the label beside it is a
        // plain noun phrase, so neither repeats the other. The announcement is
        // the counted sentence, because "6" and "Pages read" read as two
        // unrelated things when a screen reader says them in a row.
        TaffyStatTile(
            value = state.pagesRead.toString(),
            label = taffyString(R.string.taffy_task_view_failed_pages),
            modifier = Modifier.weight(1f),
            icon = TaffyIcon.Article,
            wash = TaffyTileWash.Neutral,
            accessibleDescription = taffyPlural(
                R.plurals.taffy_count_pages_read,
                state.pagesRead,
                state.pagesRead,
            ),
        )
        TaffyStatTile(
            value = state.facts.size.toString(),
            label = taffyString(R.string.taffy_task_view_failed_facts),
            modifier = Modifier.weight(1f),
            icon = TaffyIcon.Check,
            wash = TaffyTileWash.Neutral,
            accessibleDescription = taffyPlural(
                R.plurals.taffy_task_view_failed_kept,
                state.facts.size,
                state.facts.size,
            ),
        )
    }
}

/**
 * What the workspace is, said once, where the offer to discard it can be seen.
 *
 * Only when there is one to discard. A task whose workspace is already saved,
 * or which never got one, has nothing this sentence would be about.
 */
@Composable
private fun WorkspaceNote(state: TaskViewUiState) {
    if (!state.canDiscardWorkspace) return
    Row(
        modifier = Modifier.fillMaxWidth().testTag(TASK_WORKSPACE_NOTE_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        verticalAlignment = Alignment.Top,
    ) {
        Icon(
            imageVector = TaffyIcon.Info,
            contentDescription = null,
            tint = TaffyTheme.colors.textSecondary,
            modifier = Modifier.size(NoteGlyphSize),
        )
        Text(
            text = taffyString(R.string.taffy_task_view_workspace_note),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/**
 * The one sentence for a failure reason; the pill says the same one. A failed
 * task the core gave no reason for is Taffy stopping, not the pages.
 *
 * Each is a complete statement on its own, because the pill and the start
 * page's panel print it with no heading above it. The two that are not the
 * provider's, the network's or a budget's also say what the person can do: a
 * step Taffy is unsure of is checked on the page, and Taffy's own failure is
 * answered by asking again.
 */
@StringRes
internal fun taskFailureLine(failure: TaskFailureReason?): Int = when (failure) {
    TaskFailureReason.PROVIDER_UNAVAILABLE -> R.string.taffy_assistant_failed_provider
    TaskFailureReason.PROVIDER_REFUSED -> R.string.taffy_assistant_failed_refused
    TaskFailureReason.PROVIDER_LIMIT -> R.string.taffy_assistant_failed_limit
    TaskFailureReason.OFFLINE -> R.string.taffy_assistant_failed_offline
    TaskFailureReason.BUDGET_EXCEEDED -> R.string.taffy_assistant_failed_budget
    TaskFailureReason.DEADLINE_EXCEEDED -> R.string.taffy_assistant_failed_deadline
    TaskFailureReason.POLICY_REFUSED -> R.string.taffy_assistant_failed_policy
    TaskFailureReason.UNVERIFIABLE_ACTION -> R.string.taffy_assistant_failed_unverified
    TaskFailureReason.OUTCOME_UNKNOWN -> R.string.taffy_assistant_failed_outcome_unknown
    TaskFailureReason.SOURCES_UNAVAILABLE -> R.string.taffy_assistant_failed
    TaskFailureReason.INTERNAL,
    null,
    -> R.string.taffy_assistant_failed_internal
}

/**
 * The heading over that sentence: the same fact in three or four words.
 *
 * Two lengths rather than one, because the card has a heading and a body and
 * repeating one sentence in both is worse than either alone. The heading is
 * what a person reads first and it names the half that is theirs to act on —
 * a provider, a limit, the network — while the sentence stays the full
 * statement the pill also carries.
 *
 * Never the same words as [taskFailureLine]. Internal failure used to carry
 * one sentence under two names, and every step whose outcome was unknown was
 * sorted into it, so a card read "Taffy stopped unexpectedly" twice over a
 * link the browser had refused on purpose (decision 0229).
 * `TaskFailureCopyTest` compares the words themselves, because two resource
 * names can hold one sentence and a comparison of names would pass.
 */
@StringRes
internal fun taskFailureHeadline(failure: TaskFailureReason): Int = when (failure) {
    TaskFailureReason.PROVIDER_UNAVAILABLE -> R.string.taffy_task_failed_head_provider
    TaskFailureReason.PROVIDER_REFUSED -> R.string.taffy_task_failed_head_refused
    TaskFailureReason.PROVIDER_LIMIT -> R.string.taffy_task_failed_head_limit
    TaskFailureReason.OFFLINE -> R.string.taffy_task_failed_head_offline
    TaskFailureReason.BUDGET_EXCEEDED -> R.string.taffy_task_failed_head_budget
    TaskFailureReason.DEADLINE_EXCEEDED -> R.string.taffy_task_failed_head_deadline
    TaskFailureReason.POLICY_REFUSED -> R.string.taffy_task_failed_head_policy
    TaskFailureReason.UNVERIFIABLE_ACTION -> R.string.taffy_task_failed_head_unverified
    TaskFailureReason.OUTCOME_UNKNOWN -> R.string.taffy_task_failed_head_outcome_unknown
    TaskFailureReason.SOURCES_UNAVAILABLE -> R.string.taffy_task_failed_head_sources
    TaskFailureReason.INTERNAL -> R.string.taffy_task_failed_head_internal
}

/**
 * The shape beside it.
 *
 * Not one warning triangle for all eleven. The shape is the fastest part of
 * the card to read and it should not say "something is wrong" when the reason
 * already knows which kind of wrong: a key for a provider that refused, a
 * cloud for one that could not be reached, a clock for a limit, a hand for a
 * move that was not allowed. The triangle is kept for the one reason that is
 * Taffy's own fault.
 */
private fun taskFailureGlyph(failure: TaskFailureReason): ImageVector = when (failure) {
    TaskFailureReason.PROVIDER_UNAVAILABLE -> TaffyIcon.Cloud
    TaskFailureReason.PROVIDER_REFUSED -> TaffyIcon.Key
    TaskFailureReason.PROVIDER_LIMIT -> TaffyIcon.Clock
    TaskFailureReason.OFFLINE -> TaffyIcon.GlobeSimple
    TaskFailureReason.BUDGET_EXCEEDED -> TaffyIcon.Clock
    TaskFailureReason.DEADLINE_EXCEEDED -> TaffyIcon.Clock
    TaskFailureReason.POLICY_REFUSED -> TaffyIcon.Prohibit
    TaskFailureReason.UNVERIFIABLE_ACTION -> TaffyIcon.Question
    TaskFailureReason.OUTCOME_UNKNOWN -> TaffyIcon.Question
    TaskFailureReason.SOURCES_UNAVAILABLE -> TaffyIcon.Article
    TaskFailureReason.INTERNAL -> TaffyIcon.Warning
}

private val FailureGlyphSize = 28.dp
private val SetupGlyphSize = 24.dp
private val NoteGlyphSize = 16.dp

/** The reason card under a failed task's state word. */
const val TASK_FAILURE_TEST_TAG: String = "task_view_failure"

/** The two counts saying how far the task got before it stopped. */
const val TASK_FAILURE_PROGRESS_TEST_TAG: String = "task_view_failure_progress"

/** The one sentence saying what a temporary workspace is. */
const val TASK_WORKSPACE_NOTE_TEST_TAG: String = "task_view_workspace_note"

/** The set-up offer under a task that could not reach its provider. */
const val TASK_SETUP_TEST_TAG: String = "task_view_setup"
