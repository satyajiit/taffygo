// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartFailure
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * What the box says under its reading when the reading is a job for Taffy:
 * the consent, in sentences, before the person sends.
 *
 * This is the whole of what the preview screen used to say, said where the
 * request is (decision 0087 section 4): Taffy will find the site and may open
 * more; Taffy may ask for things only the person knows, and those go to the
 * site alone; and where page text goes. A refusal takes the same place and
 * says why the start is not on offer — and the two refusals a person can act
 * on from here are rows: nothing set up opens set-up, and a core that is not
 * ready asks it to start again. A failure the core answered takes the place
 * after a send. A store attached from the plus adds one sentence naming it
 * (decision 0133 section 4).
 *
 * On the Ask overlay's box the pages on the question are the consent, and
 * this is what the sheet used to say about them (decision 0135): what Taffy
 * will send of them and where, that it reads those and opens no others, that
 * two tabs on one site need one of them closed first — the browser will not
 * guess which of them was meant, so the start would be refused (decision
 * 0231) — and, when there is nothing on the question because every open page
 * is private, why.
 */
@Composable
internal fun StartPageStartLines(
    state: AddressBarUiState,
    onIntent: (AddressBarIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .then(
                if (state.conditions.asksInPlace && state.start != null) {
                    Modifier.clip(TaffyTheme.shapes.card).background(TaffyTheme.colors.ribbonTwoWash)
                        .padding(TaffyTheme.spacing.snug)
                } else {
                    Modifier.padding(horizontal = TaffyTheme.spacing.snug)
                },
            ),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        when {
            state.starting -> StartLine(
                text = taffyString(R.string.taffy_task_start_submitting),
                testTag = START_SUBMITTING_TEST_TAG,
            )
            state.startFailure != null -> StartLine(
                text = taffyString(startFailureText(state.startFailure)),
                color = TaffyTheme.colors.danger,
                testTag = START_FAILURE_TEST_TAG,
            )
        }
        when (val start = state.start) {
            is TaskStartDecision.Start -> ConsentLines(start.request.consent, state)
            is TaskStartDecision.Refused -> RefusalLine(
                reason = start.reason,
                onAct = when (start.reason) {
                    TaskStartRefusal.SETUP_NEEDED -> {
                        { state.reading?.let { onIntent(AddressBarIntent.Choose(it)) } }
                    }
                    TaskStartRefusal.CORE_NOT_READY -> {
                        { onIntent(AddressBarIntent.RetryCore) }
                    }
                    // The refusal about pages opens the place pages are
                    // chosen. It used to say "open it from the Ask sheet",
                    // which decision 0135 had already turned into this
                    // overlay — so the sentence sent a person to the surface
                    // they were reading it on, and a comparison typed on the
                    // start page had nowhere left to go. The sheet is a tap
                    // away and this is the tap.
                    //
                    // Only where that sheet is drawn, which is this overlay:
                    // `attachOpen` on a host with nothing listening to it is
                    // a control that reports success and does nothing.
                    TaskStartRefusal.WRONG_PAGE_COUNT -> if (state.conditions.asksInPlace) {
                        { onIntent(AddressBarIntent.OpenAttachPages) }
                    } else {
                        null
                    }
                    else -> null
                },
            )
            null -> Unit
        }
        // Before anything is typed, as a heads-up — and on one of Taffy's own
        // tabs whatever is typed, because the refusal above it then reads
        // "Tap to choose them" over a sheet with nothing to choose.
        if (state.reading == null || state.pages.currentTabIsTaffys) {
            noPagesLine(state)?.let { line ->
                StartLine(text = taffyString(line), testTag = START_NO_PAGES_TEST_TAG)
            }
        }
    }
}

@Composable
private fun ConsentLines(consent: TaskConsentIntent, state: AddressBarUiState) {
    val pages = state.attachedPages
    val route = routeText(consent.providerRoute)?.let { taffyString(it) }
    if (pages.isNotEmpty()) {
        // Where page text goes is part of the page sentence, so it is not
        // said again on a line of its own below.
        StartLine(
            text = if (state.conditions.asksInPlace) compactPagesSentence(pages.size, consent.providerRoute)
            else pagesSentence(pages.size, route),
            testTag = START_PAGES_TEST_TAG,
        )
        if (!consent.sourceDiscoveryEnabled) {
            StartLine(text = taffyString(
                if (state.conditions.asksInPlace) R.string.taffy_ask_scope_compact else R.string.taffy_ask_scope_exact,
            ))
        }
        if (TaskScope.summarizePages(pages).hasDuplicateSite) {
            StartLine(text = taffyString(R.string.taffy_ask_same_site))
        }
    }
    if (consent.sourceDiscoveryEnabled) {
        StartLine(
            text = taffyPlural(
                R.plurals.taffy_task_start_discovery,
                consent.newSourceCap,
                consent.newSourceCap,
            ),
            testTag = START_CONSENT_TEST_TAG,
        )
    }
    val errand = (state.start as? TaskStartDecision.Start)?.request?.template == TaskTemplate.WEB_ERRAND
    if (!state.conditions.asksInPlace || errand) {
        StartLine(text = taffyString(
            if (state.conditions.asksInPlace) R.string.taffy_ask_values_compact else R.string.taffy_task_start_values,
        ))
    }
    storesSentence(consent.attachedStores)?.let {
        StartLine(text = it, testTag = START_STORES_TEST_TAG)
    }
    if (pages.isEmpty() && route != null) StartLine(text = route)
}

@Composable
private fun compactPagesSentence(count: Int, route: ProviderRoute): String {
    val pages = taffyPlural(R.plurals.taffy_count_pages, count, count)
    return taffyString(
        when (route) {
            ProviderRoute.DIRECT_WITH_YOUR_KEY -> R.string.taffy_ask_disclosure_direct_compact
            ProviderRoute.NO_MODEL_REQUIRED -> R.string.taffy_ask_disclosure_local_compact
            ProviderRoute.NOT_CONFIGURED -> return pagesSentence(count, null)
        },
        pages,
    )
}

/**
 * "Taffy will send the title and the text of this page. Stays on this
 * device…": what leaves for the pages on the question, and where it goes.
 */
@Composable
private fun pagesSentence(count: Int, route: String?): String = if (count == 1) {
    taffyString(R.string.taffy_ask_disclosure_one_page, route.orEmpty())
} else {
    taffyString(
        R.string.taffy_ask_disclosure_many_pages,
        taffyPlural(R.plurals.taffy_count_pages, count, count),
        route.orEmpty(),
    )
}.trimEnd()

/**
 * "Taffy can search your history and bookmarks for this": the stores the
 * request carries, named in one sentence, or nothing when it carries none.
 * Three strings rather than a joined list, because a list's commas and its
 * "and" are the translator's to place.
 */
@Composable
private fun storesSentence(stores: Set<TaskAttachedStore>): String? {
    val names = stores.sortedBy { it.ordinal }.map { taffyString(storeConsentName(it)) }
    return when (names.size) {
        0 -> null
        1 -> taffyString(R.string.taffy_task_start_stores_one, names[0])
        2 -> taffyString(R.string.taffy_task_start_stores_two, names[0], names[1])
        else -> taffyString(R.string.taffy_task_start_stores_three, names[0], names[1], names[2])
    }
}

@Composable
private fun RefusalLine(reason: TaskStartRefusal, onAct: (() -> Unit)?) {
    StartLine(
        text = taffyString(refusalText(reason)),
        color = when {
            onAct != null -> TaffyTheme.colors.accent
            reason == TaskStartRefusal.PAGE_CLOSED -> TaffyTheme.colors.caution
            else -> TaffyTheme.colors.textSecondary
        },
        testTag = START_REFUSAL_TEST_TAG,
        onClick = onAct,
    )
}

@Composable
private fun StartLine(
    text: String,
    color: Color = TaffyTheme.colors.textSecondary,
    testTag: String? = null,
    onClick: (() -> Unit)? = null,
) {
    Text(
        text = text,
        style = TaffyTheme.typography.caption,
        color = color,
        modifier = Modifier
            .fillMaxWidth()
            .let { if (testTag != null) it.testTag(testTag) else it }
            .let { base ->
                if (onClick == null) {
                    base
                } else {
                    base
                        .semantics { role = Role.Button }
                        .clickable(onClick = onClick)
                }
            },
    )
}

/**
 * Where page text goes for this route, or nothing to say while no route is
 * chosen — a start the rule refuses before this is read.
 */
internal fun routeText(route: ProviderRoute): Int? = when (route) {
    ProviderRoute.DIRECT_WITH_YOUR_KEY -> R.string.taffy_task_start_route_direct
    ProviderRoute.NO_MODEL_REQUIRED -> R.string.taffy_task_start_route_no_model
    ProviderRoute.NOT_CONFIGURED -> null
}

internal fun refusalText(reason: TaskStartRefusal): Int = when (reason) {
    TaskStartRefusal.NO_GOAL -> R.string.taffy_task_start_refused_no_goal
    TaskStartRefusal.SETUP_NEEDED -> R.string.taffy_task_start_refused_setup_needed
    TaskStartRefusal.NOT_READY_YET -> R.string.taffy_task_start_refused_not_ready
    TaskStartRefusal.CORE_NOT_READY -> R.string.taffy_task_start_refused_core_not_ready
    TaskStartRefusal.ALREADY_RUNNING -> R.string.taffy_task_start_refused_already_running
    TaskStartRefusal.WRONG_PAGE_COUNT -> R.string.taffy_task_start_refused_wrong_page_count
    TaskStartRefusal.PAGE_CLOSED -> R.string.taffy_ask_disclosure_closed
}

/**
 * One sentence per closed refusal the core can answer a start with.
 *
 * "Taffy could not read this request" is only for a request that could not be
 * read. A start refused because of the person's tabs or windows says what is
 * wrong and what to do instead (decision 0231): it used to share that
 * sentence, and it asked for a change no rewording could make.
 */
internal fun startFailureText(failure: TaskStartFailure): Int = when (failure) {
    TaskStartFailure.INVALID_REQUEST -> R.string.taffy_task_start_failed_invalid_request
    TaskStartFailure.STALE_GENERATION -> R.string.taffy_task_start_failed_stale_generation
    TaskStartFailure.STALE_REVISION -> R.string.taffy_task_start_failed_stale_revision
    TaskStartFailure.DEADLINE_EXCEEDED -> R.string.taffy_task_start_failed_deadline_exceeded
    TaskStartFailure.BACKPRESSURE -> R.string.taffy_task_start_failed_backpressure
    TaskStartFailure.CORE_UNAVAILABLE -> R.string.taffy_task_start_failed_core_unavailable
    TaskStartFailure.DUPLICATE -> R.string.taffy_task_start_failed_duplicate
    TaskStartFailure.PROTOCOL_VIOLATION -> R.string.taffy_task_start_failed_protocol_violation
    TaskStartFailure.SOURCE_NOT_OPEN -> R.string.taffy_task_start_failed_source_not_open
    TaskStartFailure.SOURCE_AMBIGUOUS -> R.string.taffy_task_start_failed_source_ambiguous
    TaskStartFailure.WINDOW_UNAVAILABLE -> R.string.taffy_task_start_failed_window_unavailable
}

/** The tags the semantics tests name. */
const val START_CONSENT_TEST_TAG: String = "start_page_start_consent"
const val START_STORES_TEST_TAG: String = "start_page_start_stores"
const val START_REFUSAL_TEST_TAG: String = "start_page_start_refusal"
const val START_SUBMITTING_TEST_TAG: String = "start_page_start_submitting"
const val START_FAILURE_TEST_TAG: String = "start_page_start_failure"
const val START_PAGES_TEST_TAG: String = "start_page_start_pages"
const val START_NO_PAGES_TEST_TAG: String = "start_page_start_no_pages"
