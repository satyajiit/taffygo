// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.annotation.VisibleForTesting
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.atomic.AtomicBoolean
import taffy.core_api.CoreStatusPayloadDecodeResult
import taffy.core_api.decodeCoreStatusPayload

/**
 * One ordered decode lane for the profile's bounded CoreStatus snapshots.
 *
 * A complete status can contain every retained Library, Memory, provider and
 * task row allowed by the contract. Decoding it in a generated Mojo callback
 * would make Chromium's UI thread allocate and validate that whole graph.
 * This owner keeps the cost off that thread while preserving observer order.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
interface CoreStatusDecodeWorker : AutoCloseable {
    /**
     * Returns false only when this worker no longer accepts work. A queued
     * request may be replaced by a newer request before decoding starts.
     */
    fun submit(
        payload: ByteArray,
        completion: (CoreStatusPayloadDecodeResult?) -> Unit,
    ): Boolean
}

private class SerialCoreStatusDecodeWorker(
    private val executor: ExecutorService = Executors.newSingleThreadExecutor { command ->
        Thread(command, THREAD_NAME).apply { isDaemon = true }
    },
    private val decode: (ByteArray) -> CoreStatusPayloadDecodeResult = ::decodeCoreStatusPayload,
) : CoreStatusDecodeWorker {
    private val closed = AtomicBoolean(false)
    private val queueLock = Any()
    private var running = false
    private var pending: DecodeRequest? = null

    override fun submit(
        payload: ByteArray,
        completion: (CoreStatusPayloadDecodeResult?) -> Unit,
    ): Boolean = synchronized(queueLock) {
        if (closed.get()) return@synchronized false
        val request = DecodeRequest(payload, completion)
        if (running) {
            // State snapshots are complete and immutable. Only the newest
            // not-yet-started snapshot can affect a StateFlow reader, so one
            // pending slot bounds both retained payload bytes and stale work.
            pending = request
            return@synchronized true
        }
        running = true
        try {
            executor.execute { drain(request) }
            true
        } catch (_: RejectedExecutionException) {
            running = false
            false
        }
    }

    private fun drain(initial: DecodeRequest) {
        var request = initial
        while (!closed.get()) {
            val result = try {
                decode(request.payload)
            } catch (_: RuntimeException) {
                // Generated decoding reports malformed input as a closed
                // result. An unexpected runtime failure still fails this
                // envelope closed rather than silently losing it.
                null
            }
            if (!closed.get()) {
                try {
                    request.completion(result)
                } catch (_: RuntimeException) {
                    // A consumer bug must not strand `running` forever and
                    // prevent every later core snapshot from being decoded.
                    // The ledger callback is fail-closed and never throws;
                    // this guard keeps the worker's queue invariant local.
                }
            }
            synchronized(queueLock) {
                if (closed.get()) {
                    running = false
                    pending = null
                    return
                }
                val next = pending
                if (next == null) {
                    running = false
                    return
                }
                pending = null
                request = next
            }
        }
        synchronized(queueLock) { running = false }
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        synchronized(queueLock) { pending = null }
        executor.shutdownNow()
    }

    private data class DecodeRequest(
        val payload: ByteArray,
        val completion: (CoreStatusPayloadDecodeResult?) -> Unit,
    )

    private companion object {
        const val THREAD_NAME = "TaffyCoreStatusDecode"
    }
}

/** Constructs the production ordered decoder while allowing deterministic host decoding tests. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun serialCoreStatusDecodeWorker(
    decode: (ByteArray) -> CoreStatusPayloadDecodeResult = ::decodeCoreStatusPayload,
): CoreStatusDecodeWorker = SerialCoreStatusDecodeWorker(decode = decode)
