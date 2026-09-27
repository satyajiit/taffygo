// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.os.Handler
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.suspendCancellableCoroutine
import org.chromium.base.Log
import org.chromium.taffy.core_api.mojom.CoreApiSubmissionStatus

/** UI-sequences commands and resolves every generated admission exactly once. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class CoreApiSubmissionDispatcher(
    private val mainHandler: Handler,
    private val isClosed: () -> Boolean,
    private val onTransportFailure: () -> Unit,
) {
    private val ledger = CoreApiSubmissionLedger()

    suspend fun submit(call: (SubmissionCallback) -> Unit) =
        submitResult<Unit>(
            call = { callback -> call { status -> callback(status, Unit) } },
            validateAccepted = { Unit },
        )

    suspend fun <T : Any> submitResult(
        call: (ResultSubmissionCallback<T>) -> Unit,
        validateAccepted: (T?) -> T?,
    ): T =
        suspendCancellableCoroutine { continuation ->
            val posted = mainHandler.post {
                if (isClosed()) {
                    if (continuation.isActive) {
                        continuation.resumeWithException(unavailableSubmission())
                    }
                    return@post
                }
                if (!continuation.isActive) return@post
                var acceptedResult: T? = null
                val submissionId = ledger.register { failure ->
                    if (continuation.isActive) {
                        if (failure == null) {
                            val result = acceptedResult
                            if (result == null) {
                                continuation.resumeWithException(
                                    CoreApiSubmissionException(
                                        CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION,
                                    ),
                                )
                            } else {
                                continuation.resume(result)
                            }
                        } else {
                            continuation.resumeWithException(CoreApiSubmissionException(failure))
                        }
                    }
                }
                continuation.invokeOnCancellation {
                    if (!mainHandler.post { ledger.cancel(submissionId) }) {
                        ledger.cancel(submissionId)
                    }
                }
                try {
                    call { submissionStatus, value ->
                        if (submissionStatus == CoreApiSubmissionStatus.ACCEPTED) {
                            acceptedResult = validateAccepted(value)
                            if (acceptedResult == null) {
                                if (ledger.complete(
                                        submissionId,
                                        CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION,
                                    )
                                ) {
                                    onTransportFailure()
                                }
                            } else {
                                ledger.complete(submissionId)
                            }
                        } else {
                            // The browser answered no. Which status it was is
                            // the only fact the surfaces have, and they spell
                            // several of them the same way, so the number is
                            // written down here where the answer arrives.
                            Log.w(TAG, "[taffy_core_submission_status] status=$submissionStatus")
                            ledger.complete(submissionId, submissionFailure(submissionStatus))
                        }
                    }
                } catch (_: RuntimeException) {
                    // The proxy threw before anything reached the pipe: the
                    // command could not be encoded, which is a defect in the
                    // conversion above it and says nothing about the core. A
                    // closed pipe does not throw here; it reports itself on
                    // the observer's connection error. So this is one refused
                    // command with a name in the log, and the transport stays
                    // up -- when it was read as a disconnect instead, one
                    // unencodable start emptied every core surface until the
                    // process died.
                    Log.w(TAG, "[taffy_core_submission_refused] reason=encode")
                    ledger.complete(
                        submissionId,
                        CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION,
                    )
                }
            }
            if (!posted && continuation.isActive) {
                continuation.resumeWithException(unavailableSubmission())
            }
        }

    fun failAll(failure: CoreApiSubmissionException.Reason) = ledger.failAll(failure)
}

internal typealias SubmissionCallback = (Int) -> Unit
internal typealias ResultSubmissionCallback<T> = (Int, T?) -> Unit

private const val TAG = "TaffyCoreApi"

private fun unavailableSubmission() =
    CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
