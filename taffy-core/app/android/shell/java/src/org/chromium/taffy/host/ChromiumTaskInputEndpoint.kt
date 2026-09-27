// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.os.Handler
import android.os.Looper
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.api.BrowserTaskInputEndpoint
import com.taffygo.browser.ui.core.api.TaskInputClient
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.mojo.bindings.InterfaceRequest
import org.chromium.mojo.bindings.Router
import org.chromium.mojo.system.MojoException
import org.chromium.mojo.system.Pair
import org.chromium.mojo.system.impl.CoreImpl
import org.chromium.taffy.browser.TaffyFieldValueBridge
import org.chromium.taffy.browser.field_values.mojom.FieldChallengeKind
import org.chromium.taffy.browser.field_values.mojom.FieldValueRefusal
import org.chromium.taffy.browser.field_values.mojom.FieldValueRefusalReason
import org.chromium.taffy.browser.field_values.mojom.FieldValueRequest
import org.chromium.taffy.browser.field_values.mojom.FieldValueSupplyVerdict
import org.chromium.taffy.browser.field_values.mojom.TaffyFieldValueClient
import org.chromium.taffy.browser.field_values.mojom.TaffyFieldValueSurface

/** Profile-owned Android endpoint for the browser's one-use field-value vault. */
internal class ChromiumTaskInputEndpoint(profile: Profile) :
    BrowserTaskInputEndpoint,
    TaffyFieldValueClient {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val closed = AtomicBoolean(false)
    private val surface: TaffyFieldValueSurface
    private val clientRouter: Router
    private val mutableDescribed = MutableStateFlow<TaskInputClient.Described?>(null)
    private val pending = linkedMapOf<Long, CancellableContinuation<TaffyResult<Unit>>>()
    private var nextSubmissionId = 0L

    override val isAvailable: Boolean
        get() = !closed.get()

    override val described: StateFlow<TaskInputClient.Described?> = mutableDescribed.asStateFlow()

    init {
        check(Looper.myLooper() == Looper.getMainLooper()) {
            "The task-input endpoint must be created on Chromium's UI thread"
        }
        surface = TaffyFieldValueBridge.connect(profile)
        val pipe: Pair<TaffyFieldValueClient.Proxy, InterfaceRequest<TaffyFieldValueClient>> =
            TaffyFieldValueClient.MANAGER.getInterfaceRequest(CoreImpl.getInstance())
        clientRouter = TaffyFieldValueClient.MANAGER.bind(this, pipe.second)
        surface.connect(pipe.first)
    }

    override fun open(request: FieldValueRequest) {
        if (closed.get()) return
        val described = describeFieldValueRequest(request)
        if (described == null) {
            mutableDescribed.value = null
            if (request.requestId.isNotEmpty()) {
                surface.dismiss(request.requestId)
            }
            return
        }
        val previous = mutableDescribed.value
        if (previous != null && previous.requestId != described.requestId) {
            // One profile surface draws one form. Answer the displaced ask
            // with nothing instead of leaving its task waiting invisibly.
            surface.dismiss(previous.requestId)
        }
        mutableDescribed.value = described
    }

    override fun close(requestId: String, reason: Int) {
        if (mutableDescribed.value?.requestId == requestId) {
            mutableDescribed.value = null
        }
    }

    override suspend fun submitValues(
        requestId: String,
        values: Map<String, String>,
    ): TaffyResult<Unit> {
        val ordered = orderedFieldValues(mutableDescribed.value, requestId, values)
            ?: return TaffyResult.Failure(FailureReason.INVALID_REQUEST)
        return supply(requestId, ordered)
    }

    override suspend fun completeInteractive(requestId: String): TaffyResult<Unit> {
        val current = mutableDescribed.value
        if (current?.requestId != requestId ||
            current.fields.none { it.challenge == CHALLENGE_INTERACTIVE }
        ) {
            return TaffyResult.Failure(FailureReason.INVALID_REQUEST)
        }
        // The person worked in the page itself. An accepted empty answer
        // closes the ask while carrying no page or value bytes anywhere.
        return supply(requestId, emptyArray())
    }

    override fun onConnectionError(error: MojoException) {
        close()
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        val closeTransport = Runnable {
            val openRequest = mutableDescribed.value?.requestId
            mutableDescribed.value = null
            if (openRequest != null) {
                try {
                    surface.dismiss(openRequest)
                } catch (_: RuntimeException) {
                    // The pipe is already gone on a connection error.
                }
            }
            failPending(FailureReason.CORE_UNAVAILABLE)
            runAllTeardownOperations(listOf(clientRouter::close, surface::close))
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            closeTransport.run()
        } else if (!mainHandler.post(closeTransport)) {
            // No Mojo object is touched off-sequence. There can be no live UI
            // callback once its main looper refuses work.
            mutableDescribed.value = null
        }
    }

    private suspend fun supply(
        requestId: String,
        values: Array<String>,
    ): TaffyResult<Unit> = suspendCancellableCoroutine { continuation ->
        val posted = mainHandler.post {
            if (closed.get() || !continuation.isActive) {
                if (continuation.isActive) {
                    continuation.resume(TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE))
                }
                return@post
            }
            val submissionId = ++nextSubmissionId
            pending[submissionId] = continuation
            continuation.invokeOnCancellation {
                mainHandler.post { pending.remove(submissionId) }
            }
            try {
                surface.supply(requestId, values) { verdict, refused ->
                    finishSubmission(submissionId, fieldValueSubmissionResult(verdict, refused))
                }
            } catch (_: RuntimeException) {
                finishSubmission(
                    submissionId,
                    TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE),
                )
                close()
            }
        }
        if (!posted && continuation.isActive) {
            continuation.resume(TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE))
        }
    }

    private fun finishSubmission(submissionId: Long, result: TaffyResult<Unit>) {
        val continuation = pending.remove(submissionId) ?: return
        if (continuation.isActive) continuation.resume(result)
    }

    private fun failPending(reason: FailureReason) {
        val continuations = pending.values.toList()
        pending.clear()
        continuations.forEach { continuation ->
            if (continuation.isActive) continuation.resume(TaffyResult.Failure(reason))
        }
    }
}

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun describeFieldValueRequest(request: FieldValueRequest): TaskInputClient.Described? {
    if (request.requestId.isBlank() || request.taskId.isBlank() || request.host.isBlank()) {
        return null
    }
    val fields = request.fields ?: return null
    if (fields.isEmpty()) return null
    val described = ArrayList<TaskInputClient.Described.Field>(fields.size)
    val identities = hashSetOf<String>()
    for (field in fields) {
        if (field.fieldId.isBlank() || !identities.add(field.fieldId)) return null
        val challenge = challengeName(field.challenge) ?: return null
        val highlight = field.highlight
        described += TaskInputClient.Described.Field(
            id = field.fieldId,
            label = field.label,
            challenge = challenge,
            sensitive = field.masked,
            challengeImage = field.challengeImage,
            highlightLeft = highlight?.left ?: 0f,
            highlightTop = highlight?.top ?: 0f,
            highlightRight = highlight?.right ?: 0f,
            highlightBottom = highlight?.bottom ?: 0f,
        )
    }
    return TaskInputClient.Described(
        request.requestId,
        request.host,
        described,
        request.approvalLifetimeSeconds,
    )
}

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun orderedFieldValues(
    described: TaskInputClient.Described?,
    requestId: String,
    values: Map<String, String>,
): Array<String>? {
    if (described?.requestId != requestId) return null
    val expected = described.fields.mapTo(linkedSetOf()) { it.id }
    if (expected.size != described.fields.size || values.keys != expected) return null
    return described.fields.map { field -> values.getValue(field.id) }.toTypedArray()
}

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun fieldValueSubmissionResult(
    verdict: Int,
    refused: Array<FieldValueRefusal>?,
): TaffyResult<Unit> = when (verdict) {
    FieldValueSupplyVerdict.ACCEPTED -> when {
        refused == null -> TaffyResult.Failure(FailureReason.PROTOCOL_VIOLATION)
        refused.isEmpty() -> TaffyResult.Success(Unit)
        refused.any { it.reason == FieldValueRefusalReason.FIELD_MAY_NOT_BE_FILLED } ->
            TaffyResult.Failure(FailureReason.NOT_PERMITTED)
        refused.any { it.reason == FieldValueRefusalReason.FIELD_GONE } ->
            TaffyResult.Failure(FailureReason.NOT_FOUND)
        refused.all { it.reason == FieldValueRefusalReason.NOT_HELD } ->
            TaffyResult.Failure(FailureReason.INVALID_REQUEST)
        else -> TaffyResult.Failure(FailureReason.PROTOCOL_VIOLATION)
    }
    FieldValueSupplyVerdict.UNKNOWN_REQUEST -> TaffyResult.Failure(FailureReason.NOT_FOUND)
    FieldValueSupplyVerdict.MALFORMED -> TaffyResult.Failure(FailureReason.MALFORMED)
    else -> TaffyResult.Failure(FailureReason.PROTOCOL_VIOLATION)
}

private fun challengeName(challenge: Int): String? = when (challenge) {
    FieldChallengeKind.NONE -> CHALLENGE_NONE
    FieldChallengeKind.IMAGE_CHALLENGE -> CHALLENGE_IMAGE
    FieldChallengeKind.INTERACTIVE_CHALLENGE -> CHALLENGE_INTERACTIVE
    FieldChallengeKind.ONE_TIME_CODE -> CHALLENGE_ONE_TIME_CODE
    else -> null
}

private const val CHALLENGE_NONE = "none"
private const val CHALLENGE_IMAGE = "image_challenge"
private const val CHALLENGE_INTERACTIVE = "interactive_challenge"
private const val CHALLENGE_ONE_TIME_CODE = "one_time_code"
