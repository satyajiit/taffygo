// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.app.Activity
import android.os.Looper
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.ui.ReadAloud
import com.taffygo.browser.ui.core.ui.ReadAloudEvent
import com.taffygo.browser.ui.core.ui.ReadAloudSession
import java.io.Closeable
import java.util.Locale
import javax.inject.Inject

/** Window-owned Android TextToSpeech adapter for person-started answer playback. */
@TaffyWindowScope
class AndroidReadAloudAdapter @Inject constructor(
    private val activity: Activity,
) : ReadAloud, Closeable {
    private var engine: TextToSpeech? = null
    private var engineState = SpeechEngineState.UNINITIALIZED
    private var active: ActiveSpeech? = null
    private var nextIdentity = 1L
    private var closed = false

    override fun speak(text: String, onEvent: (ReadAloudEvent) -> Unit): ReadAloudSession {
        if (Looper.myLooper() != Looper.getMainLooper() || closed || text.isBlank()) {
            onEvent(ReadAloudEvent.Failed)
            return ReadAloudSession.CLOSED
        }
        cancelActive()
        val identity = nextIdentity++
        val chunks = speechTextChunks(text, TextToSpeech.getMaxSpeechInputLength())
        active = ActiveSpeech(identity, chunks, onEvent)
        onEvent(ReadAloudEvent.Preparing)
        when (engineState) {
            SpeechEngineState.READY -> speakCurrentChunk(identity, flush = true)
            SpeechEngineState.UNINITIALIZED -> initializeEngine()
            SpeechEngineState.INITIALIZING -> Unit
            SpeechEngineState.FAILED -> fail(identity)
        }
        return ReadAloudSession { runOnMain { cancel(identity) } }
    }

    private fun initializeEngine() {
        engineState = SpeechEngineState.INITIALIZING
        try {
            engine = TextToSpeech(activity.applicationContext) { status ->
                runOnMain { onEngineInitialized(status) }
            }
        } catch (_: RuntimeException) {
            engineState = SpeechEngineState.FAILED
            active?.identity?.let(::fail)
        }
    }

    private fun onEngineInitialized(status: Int) {
        if (closed) {
            engine?.shutdown()
            engine = null
            return
        }
        val speechEngine = engine
        val prepared = if (status == TextToSpeech.SUCCESS && speechEngine != null) {
            try {
                speechEngine.setOnUtteranceProgressListener(progressListener()) !=
                    TextToSpeech.ERROR &&
                    speechEngine.setLanguage(Locale.getDefault()) >= TextToSpeech.LANG_AVAILABLE
            } catch (_: RuntimeException) {
                false
            }
        } else {
            false
        }
        if (!prepared) {
            engineState = SpeechEngineState.FAILED
            active?.identity?.let(::fail)
            speechEngine?.shutdown()
            engine = null
            return
        }
        engineState = SpeechEngineState.READY
        active?.identity?.let { identity -> speakCurrentChunk(identity, flush = true) }
    }

    private fun progressListener(): UtteranceProgressListener =
        object : UtteranceProgressListener() {
            override fun onStart(utteranceId: String?) {
                runOnMain {
                    val session = activeFor(utteranceId) ?: return@runOnMain
                    if (!session.started) {
                        active = session.copy(started = true)
                        session.onEvent(ReadAloudEvent.Speaking)
                    }
                }
            }

            override fun onDone(utteranceId: String?) {
                runOnMain {
                    val session = activeFor(utteranceId) ?: return@runOnMain
                    if (session.chunkIndex == session.chunks.lastIndex) {
                        finish(session.identity)
                    } else {
                        active = session.copy(chunkIndex = session.chunkIndex + 1)
                        speakCurrentChunk(session.identity, flush = false)
                    }
                }
            }

            @Deprecated("Android still calls this overload on supported engines")
            override fun onError(utteranceId: String?) {
                runOnMain { activeFor(utteranceId)?.identity?.let(::fail) }
            }

            override fun onError(utteranceId: String?, errorCode: Int) {
                runOnMain { activeFor(utteranceId)?.identity?.let(::fail) }
            }
        }

    private fun speakCurrentChunk(identity: Long, flush: Boolean) {
        val session = active?.takeIf { it.identity == identity } ?: return
        val speechEngine = engine ?: run {
            fail(identity)
            return
        }
        val result = try {
            speechEngine.speak(
                session.chunks[session.chunkIndex],
                if (flush) TextToSpeech.QUEUE_FLUSH else TextToSpeech.QUEUE_ADD,
                null,
                utteranceId(session.identity, session.chunkIndex),
            )
        } catch (_: RuntimeException) {
            TextToSpeech.ERROR
        }
        if (result == TextToSpeech.ERROR) fail(identity)
    }

    private fun activeFor(utteranceId: String?): ActiveSpeech? {
        val session = active ?: return null
        return session.takeIf {
            utteranceId == utteranceId(session.identity, session.chunkIndex)
        }
    }

    private fun finish(identity: Long) {
        val session = active?.takeIf { it.identity == identity } ?: return
        active = null
        session.onEvent(ReadAloudEvent.Finished)
    }

    private fun fail(identity: Long) {
        val session = active?.takeIf { it.identity == identity } ?: return
        active = null
        engine?.stop()
        session.onEvent(ReadAloudEvent.Failed)
    }

    private fun cancel(identity: Long) {
        if (active?.identity != identity) return
        active = null
        engine?.stop()
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
            engine?.shutdown()
            engine = null
            engineState = SpeechEngineState.FAILED
        }
    }
}

private enum class SpeechEngineState {
    UNINITIALIZED,
    INITIALIZING,
    READY,
    FAILED,
}

private data class ActiveSpeech(
    val identity: Long,
    val chunks: List<String>,
    val onEvent: (ReadAloudEvent) -> Unit,
    val chunkIndex: Int = 0,
    val started: Boolean = false,
)

private fun utteranceId(identity: Long, chunkIndex: Int): String =
    "taffy-read-$identity-$chunkIndex"
