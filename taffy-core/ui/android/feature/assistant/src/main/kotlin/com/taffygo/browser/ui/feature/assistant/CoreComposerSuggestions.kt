// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.api.CoreApiClient
import java.util.UUID
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.getAndUpdate
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * The composer's suggestion lifecycle over the browser seam.
 *
 * One subscription to the push channel for the whole of a composer's life,
 * rather than one per request. The channel has no replay — a replayed
 * suggestion is ghost text for a sentence the person already finished — so a
 * subscription opened after a request was sent could miss the answer to it
 * entirely. Subscribing once, first, removes that race rather than narrowing
 * it, and the identity on each report is what sorts the answers out.
 *
 * [mintRequestId] is a parameter so a suite can name the requests it is
 * asserting on; the surface mints them because it is the surface that decides
 * one has been superseded.
 */
internal class CoreComposerSuggestions(
    private val core: CoreApiClient,
    private val debounceMillis: Long = COMPOSER_SUGGESTION_DEBOUNCE_MILLIS,
    private val mintRequestId: () -> String = { UUID.randomUUID().toString() },
) : ComposerSuggestionRepository {

    @OptIn(FlowPreview::class)
    override fun suggestions(typed: Flow<ComposerCaretText?>): Flow<ComposerSuggestion> =
        channelFlow {
            val turn = MutableStateFlow(ComposerSuggestionTurn())
            launch {
                core.composerCompletion.collect { report ->
                    // Discard-on-supersede lives in the fold, not here: an
                    // answer the composer has moved past leaves it unchanged.
                    turn.update { it.answered(report) }
                }
            }
            launch {
                typed
                    .distinctUntilChanged()
                    .debounce(debounceMillis)
                    .collect { asked -> ask(turn, asked) }
            }
            // The channel closes when this block and its children complete, and
            // neither collector above ever does — so the composer keeps its one
            // subscription for as long as the surface is collecting, and loses
            // it the moment it stops.
            turn.map { it.offered }.distinctUntilChanged().collect { send(it) }
        }

    /**
     * Mint an identity, move to it, and ask.
     *
     * Moving first is deliberate: the older request is superseded the moment a
     * newer one exists, so an answer to it that is already on its way is
     * refused when it lands rather than shown for an instant and replaced.
     *
     * Two composers ask for nothing, and both stop waiting rather than leaving
     * a request standing. One is a composer with no insertion point, which is a
     * range of text selected. The other is a composer with nothing before the
     * caret: there is no prefix to continue, and the core refuses a blank one
     * by the same reasoning, so asking would spend a round trip to be told what
     * is already known here.
     *
     * Neither of those mints a newer identity, so neither supersedes anything —
     * which is exactly the case decision 0097 section 3 says a surface must be
     * able to state on its own. Whatever was still owed is withdrawn rather
     * than abandoned, so the model call is stopped instead of paid for and
     * discarded.
     */
    private suspend fun ask(
        turn: MutableStateFlow<ComposerSuggestionTurn>,
        asked: ComposerCaretText?,
    ) {
        if (asked == null || asked.prefix.isBlank()) {
            withdraw(turn.getAndUpdate { it.forgotten() }.awaiting)
            return
        }
        val requestId = mintRequestId()
        turn.update { it.asked(requestId) }
        try {
            core.requestComposerCompletion(
                requestId = requestId,
                prefix = asked.prefix,
                suffix = asked.suffix,
            )
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            // The core would not take it. A suggestion that was never computed
            // shows as nothing, exactly as one that was computed and offered
            // nothing does; there is no failure to report to somebody who is
            // in the middle of typing. Nothing is withdrawn either: a request
            // that was refused is not a request that is running.
            turn.update { it.forgotten() }
        }
    }

    /**
     * Say that one request is no longer wanted.
     *
     * A withdrawal that the core will not take is nothing to report. The
     * suggestion was already going to be discarded on arrival, so a failed
     * withdrawal costs what the surface was paying before it could state one —
     * and telling somebody who is mid-sentence about it would be worse than
     * either.
     */
    private suspend fun withdraw(requestId: String?) {
        val standing = requestId ?: return
        try {
            core.cancelComposerCompletion(standing)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            // Nothing left to stop, or a core that will not be told. Either
            // way the surface has already moved on.
        }
    }
}

/** The repository this feature uses when a core is in the graph. */
internal fun composerSuggestions(core: CoreApiClient): ComposerSuggestionRepository =
    CoreComposerSuggestions(core)
