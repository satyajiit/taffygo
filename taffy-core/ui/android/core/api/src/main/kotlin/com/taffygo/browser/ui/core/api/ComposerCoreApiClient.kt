// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import kotlinx.coroutines.flow.Flow

/**
 * One composer suggestion: asking for it, withdrawing the request, and the
 * answer, which arrives on its own flow rather than in a snapshot.
 */
interface ComposerCoreApiClient {
    /**
     * The answer to one composer suggestion request.
     *
     * A flow rather than a state, for [CoreApiClient.assetProgress]'s reason
     * and one more besides. A snapshot describes what is true; a suggestion is
     * not true, it is offered (decision
     * `docs/decisions/0097-a-composer-suggestion-is-spent-from-the-persons-own-key.md`
     * section 6), and putting it in the snapshot would make every state
     * generation carry a value that changes several times a second. Nothing is
     * decided from what arrives here either: the isolated core opens no task,
     * journals no entry, and there is nothing to recover once the process ends.
     *
     * A report whose [ComposerCompletionReport.requestId] the surface has moved
     * past is discarded on arrival rather than shown, and absent text is no
     * suggestion — which a surface shows as nothing, never as a failure.
     */
    val composerCompletion: Flow<ComposerCompletionReport>

    /**
     * Ask for one composer suggestion (decision
     * `docs/decisions/0097-a-composer-suggestion-is-spent-from-the-persons-own-key.md`).
     *
     * [requestId] is minted by the surface and a newer one supersedes an older,
     * because the surface is the only place that knows a person is still
     * typing: both the debounce and the superseding belong there rather than
     * here. The answer arrives on [composerCompletion] rather than in
     * [CoreApiClient.status].
     *
     * [prefix] is what the person typed before the cursor and [suffix] what
     * stands after it, absent when nothing does. Neither carries page content,
     * history, workspace facts or another conversation, and the bound on their
     * length is the contract's rather than this caller's.
     */
    suspend fun requestComposerCompletion(
        requestId: String,
        prefix: String,
        suffix: String? = null,
    )

    /**
     * Withdraw one composer suggestion request the surface has stopped wanting
     * (decision
     * `docs/decisions/0097-a-composer-suggestion-is-spent-from-the-persons-own-key.md`
     * section 3).
     *
     * Superseding needs no help: a newer [requestComposerCompletion] displaces
     * an older one and the browser stops what it displaced. This is the other
     * half — a composer that simply stopped wanting an answer, because the
     * caret moved, the field emptied or the suggestion setting went off. Without
     * it such a surface could only mint a newer identity and discard the stale
     * answer, paying for a model call per abandoned request.
     *
     * [requestId] is one the surface already asked under. Withdrawing a request
     * that is already answered, already superseded or was never made is not an
     * error: there is simply nothing left to stop.
     */
    suspend fun cancelComposerCompletion(requestId: String)
}
