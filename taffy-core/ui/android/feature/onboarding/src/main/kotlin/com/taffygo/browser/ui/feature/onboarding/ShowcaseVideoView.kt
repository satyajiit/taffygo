// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import android.content.Context
import android.content.res.Resources
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.MediaPlayer
import android.view.Surface
import android.view.TextureView
import androidx.annotation.RawRes
import java.io.IOException
import kotlin.math.min

/** A one-purpose decoder that plays the settled showcase film once. */
internal class ShowcaseVideoView(context: Context) :
    TextureView(context), TextureView.SurfaceTextureListener {
    @RawRes
    private var resourceId: Int = Resources.ID_NULL
    private var player: MediaPlayer? = null
    private var playerPrepared = false
    private var playerCompleted = false
    private var hasAudioTrack = false
    private var videoWidth = 0
    private var videoHeight = 0
    private var videoSurface: Surface? = null
    private var firstFramePresented = false
    private var soundEnabled = false
    private var audioFocusAttempted = false
    private var audioFocusRequestActive = false
    private var hasAudioFocus = false

    private val audioManager = context.getSystemService(AudioManager::class.java)
    private val playbackAttributes = AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_MEDIA)
        .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
        .build()
    private val audioFocusRequest = AudioFocusRequest.Builder(
        AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK,
    )
        .setAudioAttributes(playbackAttributes)
        .setAcceptsDelayedFocusGain(false)
        .setOnAudioFocusChangeListener(::onAudioFocusChanged)
        .build()

    init {
        alpha = 0f
        isOpaque = false
        surfaceTextureListener = this
    }

    /** Bind one resource and play it once both player and surface are ready. */
    fun play(@RawRes requestedResourceId: Int) {
        if (requestedResourceId == Resources.ID_NULL) {
            releasePlayer()
            return
        }
        if (requestedResourceId == resourceId && player != null) {
            startPreparedPlayer()
            return
        }

        releasePlayer()
        resourceId = requestedResourceId
        val created = MediaPlayer()
        player = created
        try {
            created.setAudioAttributes(playbackAttributes)
            context.resources.openRawResourceFd(requestedResourceId).use { descriptor ->
                created.setDataSource(
                    descriptor.fileDescriptor,
                    descriptor.startOffset,
                    descriptor.length,
                )
            }
            created.isLooping = false
            created.setVolume(MutedVolume, MutedVolume)
            created.setOnPreparedListener { prepared ->
                if (prepared !== player) return@setOnPreparedListener
                playerPrepared = true
                playerCompleted = false
                hasAudioTrack = prepared.trackInfo.any { track ->
                    track.trackType == MediaPlayer.TrackInfo.MEDIA_TRACK_TYPE_AUDIO
                }
                videoWidth = prepared.videoWidth
                videoHeight = prepared.videoHeight
                updateFitTransform()
                startPreparedPlayer()
            }
            created.setOnCompletionListener { completed ->
                if (completed !== player) return@setOnCompletionListener
                playerCompleted = true
                applyPlayerVolume()
                abandonAudioFocus()
            }
            created.setOnVideoSizeChangedListener { changed, width, height ->
                if (changed !== player) return@setOnVideoSizeChangedListener
                videoWidth = width
                videoHeight = height
                updateFitTransform()
            }
            created.setOnErrorListener { failed, _, _ ->
                if (failed === player) releasePlayer()
                true
            }
            videoSurface?.let(created::setSurface)
            created.prepareAsync()
        } catch (_: IllegalArgumentException) {
            releasePlayer()
        } catch (_: IllegalStateException) {
            releasePlayer()
        } catch (_: IOException) {
            releasePlayer()
        } catch (_: Resources.NotFoundException) {
            releasePlayer()
        }
    }

    /** Change this film's volume without touching the device ringer. */
    fun setSoundEnabled(enabled: Boolean) {
        if (soundEnabled == enabled) return
        soundEnabled = enabled
        if (enabled) {
            audioFocusAttempted = false
            startPreparedPlayer()
        } else {
            applyPlayerVolume()
            abandonAudioFocus()
        }
    }

    /** Release every native object when Compose disposes this settled page. */
    fun release() {
        animate().cancel()
        releasePlayer()
        releaseSurface()
        surfaceTextureListener = null
    }

    override fun onSurfaceTextureAvailable(texture: SurfaceTexture, width: Int, height: Int) {
        releaseSurface()
        videoSurface = Surface(texture).also { surface ->
            player?.setSurface(surface)
        }
        updateFitTransform()
        startPreparedPlayer()
    }

    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
        updateFitTransform()
    }

    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
        try {
            player?.setSurface(null)
        } catch (_: IllegalStateException) {
            // A player that failed during preparation already has no surface.
        }
        abandonAudioFocus()
        applyPlayerVolume()
        releaseSurface()
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
        if (firstFramePresented) return
        firstFramePresented = true
        animate()
            .alpha(1f)
            .setDuration(FirstFrameFadeMs)
            .start()
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        super.onSizeChanged(width, height, oldWidth, oldHeight)
        updateFitTransform()
    }

    private fun startPreparedPlayer() {
        val active = player ?: return
        if (!playerPrepared || playerCompleted || videoSurface == null) return
        if (soundEnabled && hasAudioTrack) ensureAudioFocus()
        applyPlayerVolume()
        if (active.isPlaying) return
        try {
            active.start()
        } catch (_: IllegalStateException) {
            releasePlayer()
        }
    }

    private fun ensureAudioFocus() {
        if (hasAudioFocus || audioFocusAttempted || !soundEnabled || !hasAudioTrack) return
        audioFocusAttempted = true
        val result = audioManager?.requestAudioFocus(audioFocusRequest)
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            audioFocusRequestActive = true
            hasAudioFocus = true
        } else {
            hasAudioFocus = false
            applyPlayerVolume()
            audioManager?.abandonAudioFocusRequest(audioFocusRequest)
        }
    }

    private fun onAudioFocusChanged(change: Int) {
        when (change) {
            AudioManager.AUDIOFOCUS_GAIN -> {
                if (soundEnabled) {
                    audioFocusRequestActive = true
                    hasAudioFocus = true
                    applyPlayerVolume()
                } else {
                    abandonAudioFocus()
                }
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK,
            -> {
                hasAudioFocus = false
                applyPlayerVolume()
            }
            AudioManager.AUDIOFOCUS_LOSS -> loseAudioFocusForCurrentFilm()
        }
    }

    private fun loseAudioFocusForCurrentFilm() {
        val requestWasActive = audioFocusRequestActive
        audioFocusRequestActive = false
        hasAudioFocus = false
        audioFocusAttempted = true
        applyPlayerVolume()
        if (requestWasActive) audioManager?.abandonAudioFocusRequest(audioFocusRequest)
    }

    private fun applyPlayerVolume() {
        val volume = if (soundEnabled && hasAudioTrack && hasAudioFocus && !playerCompleted) {
            AudibleVolume
        } else {
            MutedVolume
        }
        try {
            player?.setVolume(volume, volume)
        } catch (_: IllegalStateException) {
            // Release owns a player that changed state between focus events.
        }
    }

    private fun abandonAudioFocus() {
        val requestWasActive = audioFocusRequestActive
        audioFocusRequestActive = false
        hasAudioFocus = false
        audioFocusAttempted = false
        applyPlayerVolume()
        if (requestWasActive) audioManager?.abandonAudioFocusRequest(audioFocusRequest)
    }

    /** Scale the texture down inside the view when aspect ratios ever differ. */
    private fun updateFitTransform() {
        if (width <= 0 || height <= 0 || videoWidth <= 0 || videoHeight <= 0) return
        val viewWidth = width.toFloat()
        val viewHeight = height.toFloat()
        val fit = min(viewWidth / videoWidth, viewHeight / videoHeight)
        val fittedWidth = videoWidth * fit
        val fittedHeight = videoHeight * fit
        val matrix = Matrix().apply {
            setScale(
                fittedWidth / viewWidth,
                fittedHeight / viewHeight,
                viewWidth / 2f,
                viewHeight / 2f,
            )
        }
        setTransform(matrix)
    }

    private fun releasePlayer() {
        val active = player
        if (active != null) {
            try {
                active.setVolume(MutedVolume, MutedVolume)
            } catch (_: IllegalStateException) {
                // An unprepared player is released below without playing.
            }
        }
        player = null
        playerPrepared = false
        playerCompleted = false
        hasAudioTrack = false
        firstFramePresented = false
        animate().cancel()
        alpha = 0f
        resourceId = Resources.ID_NULL
        videoWidth = 0
        videoHeight = 0
        applyPlayerVolume()
        abandonAudioFocus()
        if (active != null) {
            try {
                active.stop()
            } catch (_: IllegalStateException) {
                // An unprepared player cannot stop, but it still must release.
            }
            try {
                active.reset()
            } catch (_: IllegalStateException) {
                // Release still owns the native decoder when reset cannot run.
            }
            active.release()
        }
    }

    private fun releaseSurface() {
        videoSurface?.release()
        videoSurface = null
    }

    private companion object {
        const val MutedVolume = 0f
        const val AudibleVolume = 1f
        const val FirstFrameFadeMs = 140L
    }
}
