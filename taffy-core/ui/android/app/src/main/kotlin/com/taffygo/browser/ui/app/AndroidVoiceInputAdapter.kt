// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Looper
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.ui.VoiceInput
import com.taffygo.browser.ui.core.ui.VoiceInputEvent
import com.taffygo.browser.ui.core.ui.VoiceInputFailure
import com.taffygo.browser.ui.core.ui.VoiceInputSession
import com.taffygo.browser.ui.core.ui.VoiceTranscript
import java.io.Closeable
import java.util.Locale
import javax.inject.Inject

/** Android microphone and speech-service adapter for person-started voice entry. */
@TaffyWindowScope
class AndroidVoiceInputAdapter @Inject constructor(
    private val activity: Activity,
) : VoiceInput, Closeable {
    private var active: ActiveRecognition? = null
    private var nextIdentity = 1L
    private var closed = false

    override fun listen(onEvent: (VoiceInputEvent) -> Unit): VoiceInputSession {
        if (Looper.myLooper() != Looper.getMainLooper() || closed) {
            onEvent(VoiceInputEvent.Failed(VoiceInputFailure.UNAVAILABLE))
            return VoiceInputSession.CLOSED
        }
        cancelActive()
        val identity = nextIdentity++
        active = ActiveRecognition(identity, onEvent)
        if (ContextCompat.checkSelfPermission(activity, Manifest.permission.RECORD_AUDIO) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            active = active?.copy(waitingForPermission = true)
            onEvent(VoiceInputEvent.PermissionRequested)
            try {
                ActivityCompat.requestPermissions(
                    activity,
                    arrayOf(Manifest.permission.RECORD_AUDIO),
                    MICROPHONE_PERMISSION_REQUEST_CODE,
                )
            } catch (_: RuntimeException) {
                fail(identity, VoiceInputFailure.UNAVAILABLE)
            }
        } else {
            startRecognition(identity)
        }
        return VoiceInputSession { runOnMain { cancel(identity) } }
    }

    /** Returns true only for this adapter's reserved permission request. */
    fun onRequestPermissionsResult(requestCode: Int, grantResults: IntArray): Boolean {
        if (requestCode != MICROPHONE_PERMISSION_REQUEST_CODE) return false
        val session = active?.takeIf { it.waitingForPermission } ?: return true
        active = session.copy(waitingForPermission = false)
        if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) {
            startRecognition(session.identity)
        } else {
            fail(session.identity, VoiceInputFailure.PERMISSION_DENIED)
        }
        return true
    }

    private fun startRecognition(identity: Long) {
        val session = active?.takeIf { it.identity == identity } ?: return
        val recognizer = newRecognizer() ?: run {
            fail(identity, VoiceInputFailure.UNAVAILABLE)
            return
        }
        active = session.copy(recognizer = recognizer)
        try {
            recognizer.setRecognitionListener(listener(identity))
            recognizer.startListening(recognitionIntent())
        } catch (_: RuntimeException) {
            fail(identity, VoiceInputFailure.UNAVAILABLE)
        }
    }

    private fun newRecognizer(): SpeechRecognizer? {
        return try {
            if (!SpeechRecognizer.isRecognitionAvailable(activity)) return null
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                SpeechRecognizer.isOnDeviceRecognitionAvailable(activity)
            ) {
                try {
                    return SpeechRecognizer.createOnDeviceSpeechRecognizer(activity)
                } catch (_: RuntimeException) {
                    // A service can disappear between availability and creation.
                }
            }
            SpeechRecognizer.createSpeechRecognizer(activity)
        } catch (_: RuntimeException) {
            null
        }
    }

    private fun listener(identity: Long): RecognitionListener = object : RecognitionListener {
        override fun onReadyForSpeech(params: Bundle?) = emit(identity, VoiceInputEvent.Listening)

        override fun onBeginningOfSpeech() = emit(identity, VoiceInputEvent.Listening)

        override fun onRmsChanged(rmsdB: Float) = Unit

        override fun onBufferReceived(buffer: ByteArray?) = Unit

        override fun onEndOfSpeech() = emit(identity, VoiceInputEvent.Processing)

        override fun onError(error: Int) = fail(identity, recognitionFailure(error))

        override fun onResults(results: Bundle?) {
            val transcript = transcriptFrom(results)
            if (transcript == null) {
                fail(identity, VoiceInputFailure.NO_SPEECH)
            } else {
                finish(identity, VoiceInputEvent.Ready(transcript))
            }
        }

        override fun onPartialResults(partialResults: Bundle?) {
            transcriptFrom(partialResults)?.let { transcript ->
                emit(identity, VoiceInputEvent.Partial(transcript))
            }
        }

        override fun onEvent(eventType: Int, params: Bundle?) = Unit
    }

    private fun emit(identity: Long, event: VoiceInputEvent) {
        active?.takeIf { it.identity == identity }?.onEvent?.invoke(event)
    }

    private fun fail(identity: Long, reason: VoiceInputFailure) {
        finish(identity, VoiceInputEvent.Failed(reason))
    }

    private fun finish(identity: Long, event: VoiceInputEvent) {
        val session = active?.takeIf { it.identity == identity } ?: return
        active = null
        session.recognizer?.safelyDestroy(cancelFirst = false)
        session.onEvent(event)
    }

    private fun cancel(identity: Long) {
        val session = active?.takeIf { it.identity == identity } ?: return
        active = null
        session.recognizer?.safelyDestroy(cancelFirst = true)
    }

    private fun cancelActive() {
        active?.identity?.let(::cancel)
    }

    private fun runOnMain(action: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) action() else activity.runOnUiThread(action)
    }

    override fun close() {
        runOnMain {
            if (closed) return@runOnMain
            closed = true
            cancelActive()
        }
    }

    private companion object {
        // AndroidPermissionAdapter owns 0x5440..0x547f.
        const val MICROPHONE_PERMISSION_REQUEST_CODE = 0x5480
    }
}

private data class ActiveRecognition(
    val identity: Long,
    val onEvent: (VoiceInputEvent) -> Unit,
    val waitingForPermission: Boolean = false,
    val recognizer: SpeechRecognizer? = null,
)

private fun recognitionIntent(): Intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
    putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
    putExtra(RecognizerIntent.EXTRA_LANGUAGE, Locale.getDefault().toLanguageTag())
    putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
    putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
    putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
}

private fun transcriptFrom(results: Bundle?): VoiceTranscript? = results
    ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
    ?.firstOrNull()
    ?.let(VoiceTranscript::bounded)

private fun recognitionFailure(error: Int): VoiceInputFailure = when (error) {
    SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> VoiceInputFailure.PERMISSION_DENIED
    SpeechRecognizer.ERROR_SPEECH_TIMEOUT,
    SpeechRecognizer.ERROR_NO_MATCH,
    -> VoiceInputFailure.NO_SPEECH
    SpeechRecognizer.ERROR_RECOGNIZER_BUSY,
    SpeechRecognizer.ERROR_TOO_MANY_REQUESTS,
    -> VoiceInputFailure.BUSY
    SpeechRecognizer.ERROR_NETWORK_TIMEOUT,
    SpeechRecognizer.ERROR_NETWORK,
    SpeechRecognizer.ERROR_SERVER,
    SpeechRecognizer.ERROR_SERVER_DISCONNECTED,
    -> VoiceInputFailure.CONNECTION
    SpeechRecognizer.ERROR_AUDIO,
    SpeechRecognizer.ERROR_CLIENT,
    SpeechRecognizer.ERROR_LANGUAGE_NOT_SUPPORTED,
    SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE,
    -> VoiceInputFailure.UNAVAILABLE
    else -> VoiceInputFailure.OTHER
}

private fun SpeechRecognizer.safelyDestroy(cancelFirst: Boolean) {
    try {
        if (cancelFirst) cancel()
    } catch (_: RuntimeException) {
        // Teardown remains terminal even when a remote recognizer disappeared.
    }
    try {
        destroy()
    } catch (_: RuntimeException) {
        // No product-owned audio or callback remains reachable after active was cleared.
    }
}
