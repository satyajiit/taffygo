// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.withContext

/** Folds one profile's exact provider sign-in lifetimes into visible state. */
class ProviderSignInEngine(
    private val commands: ProviderSignInCommands,
) : ProviderFlowEventSink {

    private val lock = Any()
    private val attempts = mutableMapOf<String, Attempt>()
    private val signIns = MutableStateFlow<Map<String, ProviderSignInState>>(emptyMap())

    /** Every provider with a sign-in underway or a failure still showing. */
    val states: StateFlow<Map<String, ProviderSignInState>> = signIns.asStateFlow()

    /** Begin one vendor's sign-in, for a vendor this binary carries a flow for. */
    suspend fun connect(providerId: String) {
        if (ProviderSignInFlows.of(providerId) == null) return
        if (busy(providerId)) return
        start(providerId)
    }

    /** Dismiss a shown failure; the row returns to rest. */
    fun dismissFailure(providerId: String) {
        if (signIns.value[providerId] !is ProviderSignInState.Failed) return
        clear(providerId)
    }

    /**
     * Cancel one exact attempt, waiting for its start reply when necessary.
     *
     * Browser events are held while the cancellation verdict is in flight.
     * Acceptance discards them with the attempt; refusal releases the latest
     * exact event, so a terminal that won the race is never hidden.
     */
    suspend fun cancel(providerId: String): Boolean {
        val attempt = synchronized(lock) {
            val current = attempts[providerId] ?: return false
            if (current.cancelInFlight) return false
            current.cancelInFlight = true
            current
        }
        return withContext(NonCancellable) {
            try {
                val flowId = attempt.flowId.await()
                commands.cancel(flowId)
                synchronized(lock) {
                    if (attempts[providerId] === attempt) {
                        attempts.remove(providerId)
                        clear(providerId)
                    }
                }
                true
            } catch (_: Exception) {
                synchronized(lock) {
                    if (attempts[providerId] === attempt) {
                        attempt.cancelInFlight = false
                        attempt.heldEvent?.let { applyEventLocked(attempt, it) }
                        attempt.heldEvent = null
                    }
                }
                false
            }
        }
    }

    /**
     * Hand a manually entered code, or the address the vendor's tab landed on,
     * to the exact running flow (decision 0095 section 2).
     *
     * Only a PKCE flow that is waiting on its redirect can take one, and the
     * engine cannot tell a PKCE wait from a device-code wait except by what
     * the browser reported: `AwaitingAuthorization(null, null)` is a tab open
     * on the vendor's page with nothing to show, which is exactly the state
     * the fallback exists for. Anything else — no attempt, a start reply not
     * yet in, a cancel in flight, a code panel, an exchange already running —
     * answers false without asking the browser, which refuses non-PKCE
     * submissions itself anyway.
     *
     * No state changes here. Acceptance arrives as the browser's `EXCHANGING`
     * event through [onFlowEvent], the same way an intercepted redirect does,
     * so the screen moves on the browser's word rather than on this answer.
     * The command runs outside the lock: it crosses into the browser process.
     */
    suspend fun submitCode(providerId: String, entered: String): Boolean {
        val flowId = synchronized(lock) {
            val attempt = attempts[providerId] ?: return false
            if (attempt.cancelInFlight) return false
            val resolved = attempt.resolvedFlowId ?: return false
            if (signIns.value[providerId] != ProviderSignInState.AwaitingAuthorization(null, null)) {
                return false
            }
            resolved
        }
        return try {
            commands.submitCode(flowId, entered)
        } catch (error: CancellationException) {
            throw error
        } catch (_: Exception) {
            false
        }
    }

    override fun onFlowEvent(event: ProviderFlowEvent) {
        synchronized(lock) {
            val attempt = attempts[event.providerId] ?: return
            val resolved = attempt.resolvedFlowId
            if (resolved == null) {
                if (attempt.earlyEvents.containsKey(event.flowId) ||
                    attempt.earlyEvents.size < MAX_EARLY_FLOW_IDENTITIES
                ) {
                    attempt.earlyEvents[event.flowId] = event
                }
                return
            }
            if (resolved != event.flowId) return
            if (attempt.cancelInFlight) {
                attempt.heldEvent = event
            } else {
                applyEventLocked(attempt, event)
            }
        }
    }

    private suspend fun start(providerId: String) {
        val attempt = synchronized(lock) {
            if (attempts.containsKey(providerId)) return
            Attempt(providerId).also {
                attempts[providerId] = it
                set(providerId, ProviderSignInState.Starting)
            }
        }
        try {
            // Once admission is dispatched, screen teardown must not lose the
            // only flow identity with which the admitted work can be stopped.
            val flowId = withContext(NonCancellable) { commands.start(providerId) }
            val early = synchronized(lock) {
                if (attempts[providerId] !== attempt) return
                attempt.resolvedFlowId = flowId
                attempt.flowId.complete(flowId)
                attempt.earlyEvents.remove(flowId).also { attempt.earlyEvents.clear() }
            }
            early?.let(::onFlowEvent)
        } catch (error: Exception) {
            synchronized(lock) {
                if (attempts[providerId] === attempt) {
                    attempts.remove(providerId)
                    attempt.flowId.completeExceptionally(error)
                    set(
                        providerId,
                        ProviderSignInState.Failed(ProviderSignInFailure.NOT_ADMITTED),
                    )
                }
            }
            if (error is CancellationException) throw error
        }
    }

    private fun applyEventLocked(attempt: Attempt, event: ProviderFlowEvent) {
        when (event.kind) {
            ProviderFlowEventKind.AWAITING_AUTHORIZATION -> set(
                event.providerId,
                ProviderSignInState.AwaitingAuthorization(null, null),
            )

            ProviderFlowEventKind.USER_CODE_READY -> set(
                event.providerId,
                ProviderSignInState.AwaitingAuthorization(
                    event.verificationUrl,
                    event.userCode,
                ),
            )

            ProviderFlowEventKind.EXCHANGING ->
                set(event.providerId, ProviderSignInState.Exchanging)

            ProviderFlowEventKind.COMPLETED -> finishLocked(attempt, null)
            ProviderFlowEventKind.FAILED_DENIED ->
                finishLocked(attempt, ProviderSignInFailure.DENIED)

            ProviderFlowEventKind.FAILED_PROVIDER ->
                finishLocked(attempt, ProviderSignInFailure.PROVIDER_ERROR)

            ProviderFlowEventKind.FAILED_DEADLINE ->
                finishLocked(attempt, ProviderSignInFailure.TIMED_OUT)

            ProviderFlowEventKind.FAILED_UNAVAILABLE ->
                finishLocked(attempt, ProviderSignInFailure.UNAVAILABLE)
        }
    }

    private fun finishLocked(attempt: Attempt, failure: ProviderSignInFailure?) {
        if (attempts[attempt.providerId] !== attempt) return
        attempts.remove(attempt.providerId)
        if (failure == null) {
            clear(attempt.providerId)
        } else {
            set(attempt.providerId, ProviderSignInState.Failed(failure))
        }
    }

    private fun busy(providerId: String): Boolean =
        when (signIns.value[providerId]) {
            null, is ProviderSignInState.Failed, ProviderSignInState.Idle -> false
            else -> true
        }

    private fun set(providerId: String, state: ProviderSignInState) {
        signIns.update { it + (providerId to state) }
    }

    private fun clear(providerId: String) {
        signIns.update { it - providerId }
    }

    private class Attempt(val providerId: String) {
        val flowId = CompletableDeferred<String>()
        val earlyEvents = linkedMapOf<String, ProviderFlowEvent>()
        var resolvedFlowId: String? = null
        var cancelInFlight: Boolean = false
        var heldEvent: ProviderFlowEvent? = null
    }

    private companion object {
        const val MAX_EARLY_FLOW_IDENTITIES = 4
    }
}
