// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.feature.browsing.FindInPagePort
import java.io.Closeable
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.tabmodel.TabModelSelector

/**
 * Find in page over the exact selected regular Chromium tab.
 *
 * Chromium's result notification has no request identifier. Each operation
 * therefore owns a fresh bridge and exact-tab observer, and its callback also
 * carries this adapter's generation. A late final answer from an old query can
 * reach only the old callback and is rejected before it can change [matches].
 */
class ChromiumFindInPagePort @VisibleForTesting constructor(
    private val environment: Environment,
    private val dispatchers: AppDispatchers,
    private val timeoutMillis: Long,
) : FindInPagePort, Destroyable, Closeable {

    /** Selected-page seam kept small enough for a native-free Robolectric test. */
    @VisibleForTesting
    interface Environment {
        fun selectedTarget(): Target?
        fun observeSelection(onChanged: () -> Unit): Closeable
    }

    /** One exact Tab/WebContents pair. [identity] must use reference identity. */
    @VisibleForTesting
    interface Target {
        val identity: Any
        fun observeInvalidation(onInvalidated: () -> Unit): Closeable
        fun openRequest(onResult: (Update) -> Unit): Request
    }

    /** One bridge and its exact-tab result observer. */
    @VisibleForTesting
    interface Request : Closeable {
        fun start(query: String, forward: Boolean)
        fun stop(clearSelection: Boolean)
    }

    /** Primitive projection of Chromium's result, including interim updates. */
    @VisibleForTesting
    data class Update(
        val activeIndex: Int,
        val total: Int,
        val final: Boolean,
    )

    constructor(selector: TabModelSelector, dispatchers: AppDispatchers) : this(
        ChromiumFindInPageEnvironment(selector),
        dispatchers,
        FINAL_RESULT_TIMEOUT_MILLIS,
    )

    private val matchState = MutableStateFlow(NO_MATCHES)
    private val selection = environment.observeSelection(::invalidate)
    private var target: Target? = null
    private var targetLifetime: Closeable? = null
    private var request: Request? = null
    private var answer: CompletableDeferred<FindInPagePort.MatchCount>? = null
    private var query = ""
    private var generation = 0L
    private var closed = false

    override val matches: StateFlow<FindInPagePort.MatchCount> = matchState.asStateFlow()

    override val isAvailable: Boolean
        get() = !closed && environment.selectedTarget() != null

    override suspend fun find(query: String): FindInPagePort.MatchCount {
        if (query.isBlank()) {
            clear()
            return NO_MATCHES
        }
        return issue(query, forward = true, newQuery = true)
    }

    override suspend fun next(): FindInPagePort.MatchCount =
        issueCurrent(forward = true)

    override suspend fun previous(): FindInPagePort.MatchCount =
        issueCurrent(forward = false)

    override suspend fun clear() {
        withContext(dispatchers.main) { reset(clearSelection = true) }
    }

    private suspend fun issueCurrent(forward: Boolean): FindInPagePort.MatchCount =
        withContext(dispatchers.main) {
            if (query.isBlank()) return@withContext NO_MATCHES
            issueOnCurrentPage(query, forward, newQuery = false)
        }

    private suspend fun issue(
        query: String,
        forward: Boolean,
        newQuery: Boolean,
    ): FindInPagePort.MatchCount = withContext(dispatchers.main) {
        issueOnCurrentPage(query, forward, newQuery)
    }

    private suspend fun issueOnCurrentPage(
        requestedQuery: String,
        forward: Boolean,
        newQuery: Boolean,
    ): FindInPagePort.MatchCount {
        if (closed) return NO_MATCHES
        val selected = environment.selectedTarget()
        if (selected == null) {
            reset(clearSelection = true)
            return NO_MATCHES
        }
        if (target?.identity != selected.identity) {
            reset(clearSelection = true)
            target = selected
            targetLifetime = selected.observeInvalidation(::invalidate)
        } else {
            target = selected
        }

        closeActiveRequest(clearSelection = false)
        if (newQuery) matchState.value = NO_MATCHES
        query = requestedQuery
        val operationGeneration = ++generation
        val operationAnswer = CompletableDeferred<FindInPagePort.MatchCount>()
        answer = operationAnswer
        val opened = try {
            selected.openRequest { update -> accept(operationGeneration, update) }
        } catch (_: RuntimeException) {
            reset(clearSelection = true)
            return NO_MATCHES
        }
        request = opened
        try {
            opened.start(requestedQuery, forward)
        } catch (_: RuntimeException) {
            reset(clearSelection = true)
            return NO_MATCHES
        }

        return try {
            val final = withTimeoutOrNull(timeoutMillis) { operationAnswer.await() }
            if (final == null && generation == operationGeneration) {
                reset(clearSelection = true)
            }
            final ?: NO_MATCHES
        } catch (cancelled: Throwable) {
            if (generation == operationGeneration) reset(clearSelection = true)
            throw cancelled
        } finally {
            if (generation == operationGeneration && operationAnswer.isCompleted) {
                closeActiveRequest(clearSelection = false)
            }
        }
    }

    private fun accept(operationGeneration: Long, update: Update) {
        if (
            closed ||
            !update.final ||
            operationGeneration != generation ||
            environment.selectedTarget()?.identity != target?.identity
        ) {
            return
        }
        val total = update.total.coerceAtLeast(0)
        val active = update.activeIndex.takeIf { it in 1..total } ?: 0
        val final = FindInPagePort.MatchCount(active, total)
        matchState.value = final
        answer?.complete(final)
    }

    private fun invalidate() {
        if (!closed) reset(clearSelection = true)
    }

    private fun reset(clearSelection: Boolean) {
        generation += 1
        query = ""
        matchState.value = NO_MATCHES
        answer?.complete(NO_MATCHES)
        answer = null
        closeActiveRequest(clearSelection)
        targetLifetime?.closeQuietly()
        targetLifetime = null
        target = null
    }

    private fun closeActiveRequest(clearSelection: Boolean) {
        val active = request
        request = null
        answer?.complete(NO_MATCHES)
        answer = null
        if (active != null && clearSelection) {
            try {
                active.stop(clearSelection = true)
            } catch (_: RuntimeException) {
                // Closing the observer and bridge is still mandatory.
            }
        }
        active?.closeQuietly()
    }

    override fun destroy() {
        if (closed) return
        closed = true
        reset(clearSelection = true)
        selection.closeQuietly()
    }

    override fun close() = destroy()

    private fun Closeable.closeQuietly() {
        try {
            close()
        } catch (_: RuntimeException) {
            // A native half may already have gone away with its WebContents.
        }
    }

    private companion object {
        val NO_MATCHES = FindInPagePort.MatchCount()
        const val FINAL_RESULT_TIMEOUT_MILLIS = 3_000L
    }
}
