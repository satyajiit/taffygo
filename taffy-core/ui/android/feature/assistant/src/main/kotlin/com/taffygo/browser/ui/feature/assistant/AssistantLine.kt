// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The one-line copy patterns of UX spec section 4.
 *
 * Every line is a template with the real counts filled in — never page text,
 * never model text. There is one line per bar state and no default branch, so
 * a new state cannot slip through with an empty line.
 *
 * The one branch that is not a bar state is the notice, and it comes before
 * them all: six of those lines say Taffy is doing something, and a build with
 * nothing behind the executor seam may not say any of them.
 */
@Composable
@ReadOnlyComposable
fun assistantLine(state: AssistantBarUiState): String = when {
    state.invitesSetup -> taffyString(R.string.taffy_assistant_not_set_up)
    state.isIdle -> taffyString(R.string.taffy_assistant_idle)
    state.listening -> taffyString(R.string.taffy_assistant_listening)
    // Before every line that claims Taffy is doing something, because where
    // nothing is driving the task none of them is true. Which states keep their
    // own words is `AssistantBarUiState.speaksNotice`, so the choice is made
    // once, in something a host test can call; the null check beside it is
    // redundant to a reader and is what lets the compiler pass the value on.
    state.speaksNotice && state.notice != null ->
        taffyString(assistantNoticeLine(state.notice))
    else -> when (state.state) {
        // The line moves with the work: the newest step once there is one,
        // the count of pages while the first is still being read, and a
        // plain "getting started" before the task has named a page at all —
        // never "comparing 0 pages", which is a count standing in for a fact.
        TaskDisplayState.RUNNING -> {
            val phase = phaseLine(state.statusMessageKey)
            val step = state.latestStep
            when {
                phase != null -> taffyString(phase)
                step != null -> timelineLine(step)
                state.sourcesPlanned > 0 -> taffyPlural(
                    R.plurals.taffy_assistant_running,
                    state.sourcesPlanned,
                    state.sourcesPlanned,
                )
                else -> taffyString(R.string.taffy_assistant_running_start)
            }
        }
        // The same rule as RUNNING, and it was broken here in the same way.
        // The tab-approval plural was the fall-through arm, so a task waiting
        // for something that is not an approval rendered its count anyway: a
        // phone showed "Taffy needs your OK to open 0 more tabs" while the
        // field sheet under it was asking for an Aadhaar number and a
        // captcha. A wait names what is wanted, and a count is printed only
        // when there is one.
        TaskDisplayState.WAITING_FOR_YOU ->
            waitingLine(state)?.let { taffyString(it) }
                ?: taffyPlural(
                    R.plurals.taffy_assistant_waiting,
                    state.approvalCount,
                    state.approvalCount,
                )
        // A pause the provider caused names itself, so the person knows what
        // Resume would try again; a pause they asked for keeps the counts.
        // A pause Taffy caused by handing the page back is not a count at
        // all - it is a request, and the person has to be told they are the
        // one holding the page now. It read "Paused - 1 of 0 pages read",
        // which says nothing is wanted and gives a number that was never
        // real: a task that goes straight to the site it was told plans no
        // sources, so the denominator is zero and the line is a nonsense the
        // person is left to interpret. An errand that found its own pages read
        // "Paused — 3 of 1 page read" the same way, so the count is printed
        // against a plan only where `countsReadAgainstPlan` says there is one.
        TaskDisplayState.PAUSED ->
            pausedLine(state)?.let { taffyString(it) }
                ?: if (state.countsReadAgainstPlan) {
                    taffyPlural(
                        R.plurals.taffy_assistant_paused,
                        state.sourcesPlanned,
                        state.sourcesRead,
                        state.sourcesPlanned,
                    )
                } else {
                    taffyString(
                        R.string.taffy_assistant_paused_read,
                        taffyPlural(
                            R.plurals.taffy_count_pages_read,
                            state.sourcesRead,
                            state.sourcesRead,
                        ),
                    )
                }
        TaskDisplayState.DONE -> taffyString(
            R.string.taffy_assistant_done,
            taffyPlural(R.plurals.taffy_count_sources, state.sourcesRead, state.sourcesRead),
            taffyPlural(R.plurals.taffy_count_conflicts, state.conflicts, state.conflicts),
        )
        // What was read is the one count this side holds; what is missing is
        // the core's to say and is not on the wire yet, so the line does not
        // invent a number for it.
        TaskDisplayState.PARTLY_DONE -> taffyString(
            R.string.taffy_assistant_partly_done,
            taffyPlural(R.plurals.taffy_count_pages_read, state.sourcesRead, state.sourcesRead),
        )
        TaskDisplayState.STOPPED -> taffyString(R.string.taffy_assistant_stopped)
        TaskDisplayState.FAILED -> taffyString(taskFailureLine(state.failure))
        null -> taffyString(R.string.taffy_assistant_idle)
    }
}

/**
 * The line for the phase the core names, or null for a key that names no
 * phase — `task.running` and every wait — so the step and the counts speak.
 *
 * The keys are the core's closed vocabulary (`project_status_message` in the
 * core runtime), matched by exact string because the contract carries the
 * message key and nothing structured beside it. An unknown key is not an
 * error here: it is a phase this build has no sentence for, and the line
 * falls through to what it can say.
 */
@StringRes
internal fun phaseLine(statusMessageKey: String?): Int? = when (statusMessageKey) {
    "task.queued" -> R.string.taffy_assistant_phase_queued
    "task.planning" -> R.string.taffy_assistant_phase_planning
    "task.observing" -> R.string.taffy_assistant_phase_observing
    "task.thinking" -> R.string.taffy_assistant_phase_thinking
    "task.acting" -> R.string.taffy_assistant_phase_acting
    "task.verifying" -> R.string.taffy_assistant_phase_verifying
    "task.completing" -> R.string.taffy_assistant_phase_completing
    // Notices the core raises while running, each for something "thinking"
    // would hide: a paid retry, a switch of model, a reply asked for again, a
    // refused move, a plan that did not hold, a step whose outcome is unknown.
    "task.retrying_provider" -> R.string.taffy_assistant_notice_retrying
    "task.switching_model" -> R.string.taffy_assistant_notice_switching_model
    "task.asking_again" -> R.string.taffy_assistant_notice_asking_again
    "task.blocked_move" -> R.string.taffy_assistant_notice_blocked_move
    "task.plan_abandoned" -> R.string.taffy_assistant_notice_plan_abandoned
    "task.outcome_unknown" -> R.string.taffy_assistant_notice_outcome_unknown
    else -> null
}

/**
 * The bar's version of the sentence screen SCR-303 carries in full.
 *
 * It takes the notice rather than reading it off the state, so a caller cannot
 * reach it without having one; there is no branch here that can produce this
 * line for a task something is working on.
 */
@StringRes
private fun assistantNoticeLine(notice: TaskNotice): Int = when (notice) {
    TaskNotice.CORE_UNAVAILABLE -> R.string.taffy_assistant_core_unavailable
    TaskNotice.RETRY_REQUIRED -> R.string.taffy_assistant_retry_required
}

/**
 * Which line a task waiting on the person shows, or `null` when the count of
 * things a pending approval covers is the line.
 *
 * Pure, and separate from the composable, because this arm was wrong on a
 * phone and nothing could have caught it: the tab-approval plural was the
 * fall-through, so a task waiting for a field value rendered "Taffy needs your
 * OK to open 0 more tabs" over a sheet that was asking for an Aadhaar number
 * and a captcha. The two arms above it in `assistantLine` carry comments
 * saying a count may not stand in for a fact; this is that rule, here, and
 * host-testable.
 */
@StringRes
internal fun waitingLine(state: AssistantBarUiState): Int? = when {
    state.hasHandover -> R.string.taffy_assistant_handover
    state.hasInputRequest -> R.string.taffy_assistant_input
    state.hasAsk -> R.string.taffy_assistant_ask
    state.approvalCount > 0 -> null
    else -> R.string.taffy_assistant_waiting_start
}

/**
 * Which line a paused task shows, or `null` when the pages it read are the
 * line (decision 0168).
 *
 * Pure and separate from the composable for the reason [waitingLine] is: this
 * arm had no branch for a pause that is still settling, and the counts were
 * the fall-through, so a task winding down printed the numbers it had before
 * it began stopping — on a phone, "Paused — 1 of 0 pages read", which names a
 * denominator that was never real. `task.pausing` is a state of its own in the
 * core's vocabulary and the phase projection folds it into Paused, so this is
 * the only layer that can tell a settling pause from a settled one.
 *
 * The handover arm stays ahead of it deliberately: once the page has been
 * handed back, that is the fact the person acts on whether or not the pause
 * has finished settling.
 */
@StringRes
internal fun pausedLine(state: AssistantBarUiState): Int? = when {
    state.statusMessageKey == "task.paused_provider_limit" ->
        R.string.taffy_assistant_paused_limit
    state.statusMessageKey == "task.paused_provider_busy" ->
        R.string.taffy_assistant_paused_busy
    state.statusMessageKey == "task.paused_offline" -> R.string.taffy_assistant_paused_offline
    state.statusMessageKey == "task.paused_no_answer" ->
        R.string.taffy_assistant_paused_no_answer
    state.hasHandover -> R.string.taffy_assistant_handover
    state.statusMessageKey == "task.pausing" -> R.string.taffy_assistant_pausing
    else -> null
}
