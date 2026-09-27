// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Exact identity, early-event and cancellation races for provider sign-in. */
@OptIn(ExperimentalCoroutinesApi::class)
internal class ProviderSignInEngineTest {

    @Test
    fun `connect records the browser returned flow identity`() = runTest {
        val commands = RecordingCommands()
        val engine = ProviderSignInEngine(commands)

        engine.connect(VENDOR)

        assertEquals(listOf(VENDOR), commands.started)
        assertEquals(ProviderSignInState.Starting, engine.states.value[VENDOR])
        engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.EXCHANGING))
        assertEquals(ProviderSignInState.Exchanging, engine.states.value[VENDOR])
    }

    @Test
    fun `event arriving before start reply is applied only for returned identity`() = runTest {
        val commands = RecordingCommands(startGate = CompletableDeferred())
        val engine = ProviderSignInEngine(commands)
        val start = launch { engine.connect(VENDOR) }
        runCurrent()

        engine.onFlowEvent(event("stale-flow", ProviderFlowEventKind.FAILED_DENIED))
        engine.onFlowEvent(
            event(
                FLOW,
                ProviderFlowEventKind.USER_CODE_READY,
                verificationUrl = "https://vendor.test/device",
                userCode = "ABCD-EFGH",
            ),
        )
        commands.startGate?.complete(FLOW)
        start.join()

        assertEquals(
            ProviderSignInState.AwaitingAuthorization(
                "https://vendor.test/device",
                "ABCD-EFGH",
            ),
            engine.states.value[VENDOR],
        )
    }

    @Test
    fun `late old flow cannot mutate a replacement attempt`() = runTest {
        val commands = RecordingCommands()
        val engine = ProviderSignInEngine(commands)
        engine.connect(VENDOR)
        engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.COMPLETED))

        commands.nextFlow = "flow-2"
        engine.connect(VENDOR)
        engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.FAILED_DENIED))
        assertEquals(ProviderSignInState.Starting, engine.states.value[VENDOR])

        engine.onFlowEvent(event("flow-2", ProviderFlowEventKind.EXCHANGING))
        assertEquals(ProviderSignInState.Exchanging, engine.states.value[VENDOR])
    }

    @Test
    fun `cancel requested during start waits for and sends exact identity`() = runTest {
        val commands = RecordingCommands(startGate = CompletableDeferred())
        val engine = ProviderSignInEngine(commands)
        launch { engine.connect(VENDOR) }
        runCurrent()

        val cancelled = async { engine.cancel(VENDOR) }
        runCurrent()
        assertTrue(commands.cancelled.isEmpty())

        commands.startGate?.complete(FLOW)
        assertTrue(cancelled.await())
        assertEquals(listOf(FLOW), commands.cancelled)
        assertNull(engine.states.value[VENDOR])
    }

    @Test
    fun `refused cancel retains state and releases terminal that won race`() = runTest {
        val gate = CompletableDeferred<Unit>()
        val commands = RecordingCommands(cancelGate = gate, rejectCancel = true)
        val engine = ProviderSignInEngine(commands)
        engine.connect(VENDOR)
        engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.EXCHANGING))

        val cancelled = async { engine.cancel(VENDOR) }
        runCurrent()
        engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.FAILED_DEADLINE))
        assertEquals(
            "the terminal is held until the cancellation verdict",
            ProviderSignInState.Exchanging,
            engine.states.value[VENDOR],
        )

        gate.complete(Unit)
        assertFalse(cancelled.await())
        assertEquals(
            ProviderSignInState.Failed(ProviderSignInFailure.TIMED_OUT),
            engine.states.value[VENDOR],
        )
    }

    @Test
    fun `accepted cancel discards native cancellation event and repeated cancel is no op`() =
        runTest {
            val gate = CompletableDeferred<Unit>()
            val commands = RecordingCommands(cancelGate = gate)
            val engine = ProviderSignInEngine(commands)
            engine.connect(VENDOR)

            val cancelled = async { engine.cancel(VENDOR) }
            runCurrent()
            engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.FAILED_DENIED))
            assertFalse(engine.cancel(VENDOR))

            gate.complete(Unit)
            assertTrue(cancelled.await())
            assertNull(engine.states.value[VENDOR])
            engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.FAILED_DENIED))
            assertNull(engine.states.value[VENDOR])
            assertEquals(listOf(FLOW), commands.cancelled)
        }

    @Test
    fun `unknown exact identity cannot cancel or change a live flow`() = runTest {
        val commands = RecordingCommands()
        val engine = ProviderSignInEngine(commands)
        engine.connect(VENDOR)

        engine.onFlowEvent(event("other", ProviderFlowEventKind.FAILED_PROVIDER))

        assertEquals(ProviderSignInState.Starting, engine.states.value[VENDOR])
        assertTrue(engine.cancel(VENDOR))
        assertEquals(listOf(FLOW), commands.cancelled)
    }

    @Test
    fun `generation loss event fails and removes only the exact attempt`() = runTest {
        val commands = RecordingCommands()
        val engine = ProviderSignInEngine(commands)
        engine.connect(VENDOR)

        engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.FAILED_UNAVAILABLE))

        assertEquals(
            ProviderSignInState.Failed(ProviderSignInFailure.UNAVAILABLE),
            engine.states.value[VENDOR],
        )
        assertFalse(engine.cancel(VENDOR))
    }

    @Test
    fun `refused start fails once and starts no cancellable flow`() = runTest {
        val commands = RecordingCommands(rejectStart = true)
        val engine = ProviderSignInEngine(commands)

        engine.connect(VENDOR)

        assertEquals(
            ProviderSignInState.Failed(ProviderSignInFailure.NOT_ADMITTED),
            engine.states.value[VENDOR],
        )
        assertFalse(engine.cancel(VENDOR))
    }

    // The manual-code fallback (decision 0095 section 2) is offered only to a
    // redirect the browser is waiting on. Before the start reply there is no
    // flow identity to name, so the port is not asked at all.
    @Test
    fun `a code submitted before the start reply is refused without asking the browser`() =
        runTest {
            val commands = RecordingCommands(startGate = CompletableDeferred())
            val engine = ProviderSignInEngine(commands)
            val start = launch { engine.connect(VENDOR) }
            runCurrent()

            assertFalse(engine.submitCode(VENDOR, "code#state"))
            assertTrue(commands.submitted.isEmpty())

            commands.startGate?.complete(FLOW)
            start.join()
        }

    @Test
    fun `a code submitted with no attempt or after cancel is refused`() = runTest {
        val commands = RecordingCommands()
        val engine = ProviderSignInEngine(commands)
        assertFalse(engine.submitCode(VENDOR, "nothing running"))

        engine.connect(VENDOR)
        engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.AWAITING_AUTHORIZATION))
        assertTrue(engine.cancel(VENDOR))

        assertFalse(engine.submitCode(VENDOR, "too late"))
        assertTrue(commands.submitted.isEmpty())
    }

    @Test
    fun `a code is forwarded to the exact waiting flow and the browser's answer is returned`() =
        runTest {
            val commands = RecordingCommands()
            val engine = ProviderSignInEngine(commands)
            engine.connect(VENDOR)
            engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.AWAITING_AUTHORIZATION))

            commands.acceptCode = false
            assertFalse(engine.submitCode(VENDOR, "wrong"))
            commands.acceptCode = true
            assertTrue(engine.submitCode(VENDOR, "https://vendor.test/cb?code=x&state=y"))

            assertEquals(
                listOf(FLOW to "wrong", FLOW to "https://vendor.test/cb?code=x&state=y"),
                commands.submitted,
            )
            // No state of its own: the screen still waits until the browser
            // reports the exchange, exactly as it would after an intercepted
            // redirect.
            assertEquals(
                ProviderSignInState.AwaitingAuthorization(null, null),
                engine.states.value[VENDOR],
            )
            engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.EXCHANGING))
            assertEquals(ProviderSignInState.Exchanging, engine.states.value[VENDOR])
        }

    // A device flow shows a code to *enter elsewhere*; there is nothing to
    // paste back, and the browser would refuse it anyway. The engine does not
    // spend the round trip.
    @Test
    fun `a code panel and an exchange in flight take no manual code`() = runTest {
        val commands = RecordingCommands()
        val engine = ProviderSignInEngine(commands)
        engine.connect(VENDOR)

        engine.onFlowEvent(
            event(
                FLOW,
                ProviderFlowEventKind.USER_CODE_READY,
                verificationUrl = "https://vendor.test/device",
                userCode = "ABCD-EFGH",
            ),
        )
        assertFalse(engine.submitCode(VENDOR, "ABCD-EFGH"))

        engine.onFlowEvent(event(FLOW, ProviderFlowEventKind.EXCHANGING))
        assertFalse(engine.submitCode(VENDOR, "again"))
        assertTrue(commands.submitted.isEmpty())
    }

    private fun event(
        flowId: String,
        kind: ProviderFlowEventKind,
        verificationUrl: String? = null,
        userCode: String? = null,
    ) = ProviderFlowEvent(VENDOR, flowId, kind, verificationUrl, userCode)

    private class RecordingCommands(
        var startGate: CompletableDeferred<String>? = null,
        var cancelGate: CompletableDeferred<Unit>? = null,
        private val rejectStart: Boolean = false,
        var rejectCancel: Boolean = false,
    ) : ProviderSignInCommands {
        val started = mutableListOf<String>()
        val cancelled = mutableListOf<String>()
        val submitted = mutableListOf<Pair<String, String>>()
        var nextFlow: String = FLOW
        var acceptCode: Boolean = true

        override suspend fun start(providerId: String): String {
            started += providerId
            if (rejectStart) error("start refused")
            return startGate?.await() ?: nextFlow
        }

        override suspend fun cancel(flowId: String) {
            cancelled += flowId
            cancelGate?.await()
            if (rejectCancel) error("cancel refused")
        }

        override suspend fun submitCode(flowId: String, entered: String): Boolean {
            submitted += flowId to entered
            return acceptCode
        }
    }

    private companion object {
        const val VENDOR = "xai"
        const val FLOW = "provider-flow-1"
    }
}
