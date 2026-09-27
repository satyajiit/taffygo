// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.api.CoreApiEndpointFailure
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.chromium.base.Log
import taffy.core_api.CORE_STATUS_PAYLOAD_SCHEMA_VERSION
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusPayloadDecodeResult

/**
 * Thread-safe ledger that turns observer snapshots into the one [CoreStatus] a
 * surface reads, and refuses any envelope that cannot be believed.
 *
 * It is separate from the endpoint for the same reason [CoreApiSubmissionLedger]
 * is: ordering and decoding a snapshot is a whole concern with its own state —
 * a generation, a sequence and the last good status — and none of it needs the
 * transport. Keeping it here means the endpoint holds the mojo pipe and this
 * holds the reading, and neither has to be read to understand the other.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class CoreApiSnapshotLedger(
    private val decoder: CoreStatusDecodeWorker = serialCoreStatusDecodeWorker(),
) : AutoCloseable {
    private val lock = Any()
    private val mutableStatus = MutableStateFlow(unavailableCoreStatus(0uL))
    private val mutableFailure = MutableStateFlow<CoreApiEndpointFailure?>(null)
    @Volatile private var latestGenerationBits = 0L
    private var latestSequence = 0uL
    private var pendingToken = 0uL
    private var closed = false

    val status: StateFlow<CoreStatus> = mutableStatus.asStateFlow()
    val endpointFailure: StateFlow<CoreApiEndpointFailure?> = mutableFailure.asStateFlow()

    /** The newest generation this ledger has accepted, for a caller reporting a fault. */
    val generation: ULong
        get() = latestGenerationBits.toULong()

    fun fail(failure: CoreApiEndpointFailure, generation: ULong) {
        synchronized(lock) {
            if (closed) return
            failLocked(failure, generation)
        }
    }

    private fun failLocked(
        failure: CoreApiEndpointFailure,
        generation: ULong,
        sequence: ULong = latestSequence,
    ) {
        val latestGeneration = latestGenerationBits.toULong()
        val effectiveGeneration = maxOf(generation, latestGeneration)
        if (effectiveGeneration > latestGeneration) {
            latestGenerationBits = effectiveGeneration.toLong()
            latestSequence = 0uL
        }
        // The kind, the generation and the sequence it happened at: enough to
        // tell a stale replay from a payload the decoder refused, and nothing
        // from the payload itself. Without this line the ledger's answer to
        // every failure was one unavailable status and silence, and a ready
        // core is quiescent, so nothing ever corrected it.
        Log.w(
            TAG,
            "[taffy_core_snapshot_refused] kind=%s generation=%d sequence=%d",
            failure.describe(),
            effectiveGeneration.toLong(),
            sequence.toLong(),
        )
        pendingToken += 1uL
        mutableStatus.value = unavailableCoreStatus(effectiveGeneration)
        mutableFailure.value = failure
    }

    fun accept(
        availabilityWire: Int,
        generationWire: Long,
        sequenceWire: Long,
        schemaVersionWire: Int,
        payload: ByteArray?,
    ) {
        val availability = CoreAvailability.fromWire(availabilityWire.toUInt())
            ?: return fail(CoreApiEndpointFailure.EnvelopeMismatch, generationWire.toULong())
        val generation = generationWire.toULong()
        val sequence = sequenceWire.toULong()
        synchronized(lock) {
            if (closed) return
            acceptLocked(
                availability,
                generation,
                sequence,
                schemaVersionWire.toUInt(),
                payload,
            )
        }
    }

    private fun acceptLocked(
        availability: CoreAvailability,
        generation: ULong,
        sequence: ULong,
        schemaVersion: UInt,
        payload: ByteArray?,
    ) {
        val latestGeneration = latestGenerationBits.toULong()
        if (generation < latestGeneration ||
            (payload != null && generation == latestGeneration && sequence <= latestSequence)
        ) {
            failLocked(CoreApiEndpointFailure.StaleSnapshot, generation, sequence)
            return
        }
        if (generation > latestGeneration) latestSequence = 0uL
        latestGenerationBits = generation.toLong()

        if (payload == null) {
            // A ready core with no payload would destroy the state it follows:
            // the payload-carrying snapshot arrives first and a ready core is
            // then quiescent, so nothing would ever correct it. Readiness is
            // announced only by the snapshot that carries the state.
            if (availability == CoreAvailability.READY) {
                failLocked(CoreApiEndpointFailure.MissingReadyPayload, generation)
                return
            }
            pendingToken += 1uL
            mutableStatus.value = unavailableCoreStatus(generation, availability)
            mutableFailure.value = null
            return
        }
        if (schemaVersion != CORE_STATUS_PAYLOAD_SCHEMA_VERSION) {
            failLocked(CoreApiEndpointFailure.InvalidPayload(null), generation)
            return
        }
        latestSequence = sequence
        pendingToken += 1uL
        val token = pendingToken
        if (!decoder.submit(payload) { decoded ->
                acceptDecoded(token, decoded, availability, generation)
            }
        ) {
            failLocked(CoreApiEndpointFailure.InvalidPayload(null), generation)
        }
    }

    private fun acceptDecoded(
        token: ULong,
        decoded: CoreStatusPayloadDecodeResult?,
        availability: CoreAvailability,
        generation: ULong,
    ) {
        synchronized(lock) {
            if (closed || token != pendingToken) return
            acceptDecodedLocked(decoded, availability, generation)
        }
    }

    private fun acceptDecodedLocked(
        decoded: CoreStatusPayloadDecodeResult?,
        availability: CoreAvailability,
        generation: ULong,
    ) {
        when (decoded) {
            null -> failLocked(CoreApiEndpointFailure.InvalidPayload(null), generation)
            is CoreStatusPayloadDecodeResult.Failure ->
                failLocked(CoreApiEndpointFailure.InvalidPayload(decoded.error), generation)
            is CoreStatusPayloadDecodeResult.Success -> {
                // The envelope and the payload each state the generation and the
                // availability. They are compared rather than trusted, because a
                // pair that disagrees is two readings of one core and there is no
                // way to tell which is the stale one.
                if (decoded.value.generation != generation ||
                    decoded.value.availability != availability
                ) {
                    failLocked(CoreApiEndpointFailure.EnvelopeMismatch, generation)
                    return
                }
                mutableStatus.value = decoded.value
                mutableFailure.value = null
            }
        }
    }

    override fun close() {
        synchronized(lock) {
            if (closed) return
            closed = true
            pendingToken += 1uL
        }
        decoder.close()
    }
}

private const val TAG = "TaffyCoreApi"

/** The failure's kind and, for a refused payload, the codec's reason. Never the payload. */
private fun CoreApiEndpointFailure.describe(): String = when (this) {
    is CoreApiEndpointFailure.InvalidPayload -> "InvalidPayload/" + (reason?.toString() ?: "none")
    else -> this::class.simpleName ?: "unknown"
}
