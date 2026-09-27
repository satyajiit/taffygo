// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.api.CoreApiEndpointFailure
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import taffy.core_api.AssistantAbilityView
import taffy.core_api.BuiltinSkillAvailabilityView
import taffy.core_api.BuiltinSkillIdView
import taffy.core_api.BuiltinSkillReferenceView
import taffy.core_api.BuiltinSkillView
import taffy.core_api.CORE_STATUS_PAYLOAD_SCHEMA_VERSION
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatusPayloadDecodeResult
import taffy.core_api.CoreStatusPayloadCodecError
import taffy.core_api.CoreStatusPayloadEncodeResult
import taffy.core_api.TaskTemplateId
import taffy.core_api.WorkspacePhase
import taffy.core_api.WorkspaceViewState
import taffy.core_api.decodeCoreStatusPayload
import taffy.core_api.encodeCoreStatusPayload

@RunWith(BaseRobolectricTestRunner::class)
class CoreApiSnapshotLedgerTest {
    @Test
    fun adjacentUnicodeStringsRoundTripThroughGeneratedCodec() {
        val status = unavailableCoreStatus(6uL, CoreAvailability.READY).copy(
            // A COMPLETE projection must enumerate every built-in skill in
            // BuiltinSkillIdView order — the contract's ordered_enum_list rule,
            // and there is no mode in which the list may be empty. The
            // unavailableCoreStatus helper leaves it empty because a shell that
            // has heard nothing from the core has nothing to put there, and it
            // never encodes what it builds. This test does encode, so it
            // supplies the roster the wire form requires.
            builtin_skills = everyBuiltinSkill(),
            workspaces = listOf(
                WorkspaceViewState(
                    workspace_id = "workspace-unicode",
                    revision = 3uL,
                    goal = "Compare cafés 🍵",
                    phase = WorkspacePhase.DONE,
                    last_updated_epoch_ms = 9uL,
                    template_id = TaskTemplateId.COMPARE_PRODUCTS,
                    sources = emptyList(),
                    facts = emptyList(),
                    saved = true,
                    display_name = "München notes",
                    deletion_preview = null,
                ),
            ),
        )

        val encoded = encodeCoreStatusPayload(status)
        // Carry the codec's own reason into the failure message; a bare
        // assertTrue here reports only that encoding did not succeed.
        assertTrue(
            "encode did not succeed: $encoded",
            encoded is CoreStatusPayloadEncodeResult.Success,
        )
        val decoded = decodeCoreStatusPayload(
            (encoded as CoreStatusPayloadEncodeResult.Success).bytes,
        )

        assertEquals(CoreStatusPayloadDecodeResult.Success(status), decoded)
    }

    @Test
    fun payloadDecodeCompletesOutsideCallingThread() {
        val caller = Thread.currentThread().name
        val callback = CountDownLatch(1)
        var callbackThread = caller
        val worker = serialCoreStatusDecodeWorker()

        assertTrue(worker.submit(byteArrayOf()) {
            callbackThread = Thread.currentThread().name
            callback.countDown()
        })
        assertTrue(callback.await(2, TimeUnit.SECONDS))
        worker.close()

        assertNotEquals(caller, callbackThread)
    }

    @Test
    fun decodeWorkerKeepsOnlyTheNewestWaitingSnapshot() {
        val firstStarted = CountDownLatch(1)
        val releaseFirst = CountDownLatch(1)
        val completed = CountDownLatch(2)
        val decoded = Collections.synchronizedList(mutableListOf<Byte>())
        val worker = serialCoreStatusDecodeWorker(decode = { payload ->
            decoded += payload.single()
            if (payload.single() == 1.toByte()) {
                firstStarted.countDown()
                assertTrue(releaseFirst.await(2, TimeUnit.SECONDS))
            }
            CoreStatusPayloadDecodeResult.Failure(CoreStatusPayloadCodecError.INVALID_MAGIC)
        })

        assertTrue(worker.submit(byteArrayOf(1)) { completed.countDown() })
        assertTrue(firstStarted.await(2, TimeUnit.SECONDS))
        assertTrue(worker.submit(byteArrayOf(2)) { completed.countDown() })
        assertTrue(worker.submit(byteArrayOf(3)) { completed.countDown() })
        releaseFirst.countDown()

        assertTrue(completed.await(2, TimeUnit.SECONDS))
        worker.close()
        assertEquals(listOf<Byte>(1, 3), decoded)
    }

    @Test
    fun completionFailureDoesNotStrandTheDecodeLane() {
        val secondCompleted = CountDownLatch(1)
        val worker = serialCoreStatusDecodeWorker(decode = {
            CoreStatusPayloadDecodeResult.Failure(CoreStatusPayloadCodecError.INVALID_MAGIC)
        })

        assertTrue(worker.submit(byteArrayOf(1)) { error("consumer failure") })
        assertTrue(worker.submit(byteArrayOf(2)) { secondCompleted.countDown() })

        assertTrue(secondCompleted.await(2, TimeUnit.SECONDS))
        worker.close()
    }

    @Test
    fun newestSnapshotWinsWhenAnOlderDecodeCompletesLate() {
        val decoder = ControlledDecodeWorker()
        val ledger = CoreApiSnapshotLedger(decoder)

        ledger.accept(readyWire(), 7L, 1L, schemaWire(), byteArrayOf(1))
        ledger.accept(readyWire(), 7L, 2L, schemaWire(), byteArrayOf(2))
        decoder.complete(0, readyResult(7uL))

        assertEquals(0uL, ledger.status.value.generation)
        decoder.complete(1, readyResult(7uL))
        assertEquals(7uL, ledger.status.value.generation)
        assertEquals(CoreAvailability.READY, ledger.status.value.availability)
        assertNull(ledger.endpointFailure.value)
        ledger.close()
    }

    @Test
    fun disconnectInvalidatesDecodeAlreadyInFlight() {
        val decoder = ControlledDecodeWorker()
        val ledger = CoreApiSnapshotLedger(decoder)

        ledger.accept(readyWire(), 9L, 1L, schemaWire(), byteArrayOf(1))
        ledger.fail(CoreApiEndpointFailure.Disconnected, 9uL)
        decoder.complete(0, readyResult(9uL))

        assertEquals(CoreAvailability.UNAVAILABLE, ledger.status.value.availability)
        assertEquals(9uL, ledger.status.value.generation)
        assertEquals(9uL, ledger.generation)
        assertEquals(CoreApiEndpointFailure.Disconnected, ledger.endpointFailure.value)
        ledger.close()
    }

    @Test
    fun staleGenerationFailsClosedWithoutMovingTheIncarnationBackwards() {
        val decoder = ControlledDecodeWorker()
        val ledger = CoreApiSnapshotLedger(decoder)

        ledger.accept(readyWire(), 9L, 1L, schemaWire(), byteArrayOf(1))
        decoder.complete(0, readyResult(9uL))
        ledger.accept(readyWire(), 8L, 1L, schemaWire(), byteArrayOf(2))

        assertEquals(CoreAvailability.UNAVAILABLE, ledger.status.value.availability)
        assertEquals(9uL, ledger.status.value.generation)
        assertEquals(CoreApiEndpointFailure.StaleSnapshot, ledger.endpointFailure.value)
        assertEquals(1, decoder.pending.size)
        ledger.close()
    }

    @Test
    fun transportFailureAdvancesTheFenceAndRejectsAnOlderRecoverySnapshot() {
        val decoder = ControlledDecodeWorker()
        val ledger = CoreApiSnapshotLedger(decoder)

        ledger.fail(CoreApiEndpointFailure.Disconnected, 12uL)
        ledger.accept(readyWire(), 11L, 1L, schemaWire(), byteArrayOf(1))

        assertEquals(12uL, ledger.generation)
        assertEquals(12uL, ledger.status.value.generation)
        assertEquals(CoreApiEndpointFailure.StaleSnapshot, ledger.endpointFailure.value)
        assertTrue(decoder.pending.isEmpty())
        ledger.close()
    }

    @Test
    fun invalidSchemaNeverReachesDecoder() {
        val decoder = ControlledDecodeWorker()
        val ledger = CoreApiSnapshotLedger(decoder)

        ledger.accept(readyWire(), 4L, 1L, schemaWire() + 1, byteArrayOf(1))

        assertTrue(decoder.pending.isEmpty())
        assertTrue(ledger.endpointFailure.value is CoreApiEndpointFailure.InvalidPayload)
        ledger.close()
    }

    @Test
    fun closeRejectsWorkAndPreventsLatePublication() {
        val decoder = ControlledDecodeWorker()
        val ledger = CoreApiSnapshotLedger(decoder)

        ledger.accept(readyWire(), 3L, 1L, schemaWire(), byteArrayOf(1))
        ledger.close()
        decoder.complete(0, readyResult(3uL))
        ledger.accept(readyWire(), 4L, 1L, schemaWire(), byteArrayOf(2))

        assertTrue(decoder.closed)
        assertEquals(0uL, ledger.status.value.generation)
        assertEquals(1, decoder.pending.size)
    }

    private class ControlledDecodeWorker : CoreStatusDecodeWorker {
        data class Pending(
            val completion: (CoreStatusPayloadDecodeResult?) -> Unit,
        )

        val pending = mutableListOf<Pending>()
        var closed = false

        override fun submit(
            payload: ByteArray,
            completion: (CoreStatusPayloadDecodeResult?) -> Unit,
        ): Boolean {
            if (closed) return false
            pending += Pending(completion)
            return true
        }

        fun complete(index: Int, result: CoreStatusPayloadDecodeResult?) {
            pending[index].completion(result)
        }

        override fun close() {
            closed = true
        }
    }

    private companion object {
        fun readyWire() = CoreAvailability.READY.wire.toInt()
        fun schemaWire() = CORE_STATUS_PAYLOAD_SCHEMA_VERSION.toInt()
        fun readyResult(generation: ULong) = CoreStatusPayloadDecodeResult.Success(
            unavailableCoreStatus(generation, CoreAvailability.READY),
        )
    }

    // Every BuiltinSkillIdView member, in declaration order, with the counts a
    // skill that needs nothing would carry. The values are incidental: the rule
    // under test constrains the identifiers and their order, nothing else.
    private fun everyBuiltinSkill(): List<BuiltinSkillView> =
        BuiltinSkillIdView.entries.map { id ->
            BuiltinSkillView(
                reference = BuiltinSkillReferenceView(skill_id = id, version = 1u),
                required_ability = AssistantAbilityView.PAGES_LOOKUP,
                enabled = true,
                availability = BuiltinSkillAvailabilityView.AVAILABLE,
                required_tool_count = 0u,
                available_tool_count = 0u,
                required_part_count = 0u,
                installed_part_count = 0u,
            )
        }

}
