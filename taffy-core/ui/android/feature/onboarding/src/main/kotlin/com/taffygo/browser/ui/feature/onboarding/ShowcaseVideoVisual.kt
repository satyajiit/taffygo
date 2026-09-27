// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import android.annotation.SuppressLint
import android.content.Context
import android.content.res.Resources
import android.graphics.BitmapFactory
import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import kotlinx.coroutines.withContext

/**
 * A theme-matched 4:5 feature film, without simulated browser chrome.
 *
 * A prefetched page decodes its first-frame poster; reduced motion uses the
 * completed result. The settled page creates one platform decoder, and leaving
 * the screen or pausing its lifecycle removes and releases it.
 */
@Composable
internal fun ShowcaseVideoVisual(
    video: ShowcaseVideo,
    videoDescription: String,
    playing: Boolean,
    showFinalPoster: Boolean,
    soundEnabled: Boolean,
    modifier: Modifier = Modifier,
) {
    val isDark = TaffyTheme.isDark
    val videoResourceId = video.videoResourceId(isDark)
    val posterResourceId = video.posterResourceId(isDark, finalFrame = showFinalPoster)
    val lifecycleResumed = rememberShowcaseLifecycleResumed()
    val shouldPlay = playing && lifecycleResumed
    val shouldDecode = shouldDecodeShowcase(
        playing = shouldPlay,
        showFinalPoster = showFinalPoster,
        soundEnabled = soundEnabled,
    )

    BoxWithConstraints(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center,
    ) {
        val availableRatio = maxWidth.value / maxHeight.value
        val filmWidth = if (availableRatio > ShowcaseAspectRatio) {
            maxHeight * ShowcaseAspectRatio
        } else {
            maxWidth
        }
        val edgeFeather = minOf(MaxEdgeFeather, filmWidth * EdgeFeatherFraction)
        val fitted = if (availableRatio > ShowcaseAspectRatio) {
            Modifier
                .fillMaxHeight()
                .aspectRatio(ShowcaseAspectRatio)
        } else {
            Modifier
                .fillMaxWidth()
                .aspectRatio(ShowcaseAspectRatio)
        }
        Box(
            modifier = fitted
                // H.264's YUV conversion can move a flat RGB edge by a few
                // channel values. The native surface below and feather above
                // keep that decoder-dependent drift from becoming a seam.
                .background(TaffyTheme.colors.surface)
                .testTag(SHOWCASE_VISUAL_TEST_TAG)
                .semantics { contentDescription = videoDescription },
        ) {
            ShowcasePoster(
                resourceId = posterResourceId,
                modifier = Modifier.fillMaxSize(),
            )
            if (shouldDecode) {
                AndroidView(
                    factory = { viewContext ->
                        ShowcaseVideoView(viewContext).apply {
                            setSoundEnabled(soundEnabled)
                            play(videoResourceId)
                        }
                    },
                    // Reduced motion keeps the completed poster visible while
                    // the settled decoder supplies narration underneath it.
                    modifier = Modifier
                        .fillMaxSize()
                        .graphicsLayer { alpha = if (showFinalPoster) 0f else 1f },
                    onRelease = { view -> view.release() },
                    update = { view ->
                        view.setSoundEnabled(soundEnabled)
                        view.play(videoResourceId)
                    },
                )
            }
            ShowcaseEdgeFeather(depth = edgeFeather)
        }
    }
}

internal fun shouldDecodeShowcase(
    playing: Boolean,
    showFinalPoster: Boolean,
    soundEnabled: Boolean,
): Boolean = playing && (!showFinalPoster || soundEnabled)

@Composable
private fun BoxScope.ShowcaseEdgeFeather(depth: Dp) {
    val surface = TaffyTheme.colors.surface
    val clear = surface.copy(alpha = 0f)
    Box(
        modifier = Modifier
            .align(Alignment.TopCenter)
            .fillMaxWidth()
            .height(depth)
            .background(Brush.verticalGradient(listOf(surface, clear))),
    )
    Box(
        modifier = Modifier
            .align(Alignment.BottomCenter)
            .fillMaxWidth()
            .height(depth)
            .background(Brush.verticalGradient(listOf(clear, surface))),
    )
    Box(
        modifier = Modifier
            .align(Alignment.CenterStart)
            .fillMaxHeight()
            .width(depth)
            .background(Brush.horizontalGradient(listOf(surface, clear))),
    )
    Box(
        modifier = Modifier
            .align(Alignment.CenterEnd)
            .fillMaxHeight()
            .width(depth)
            .background(Brush.horizontalGradient(listOf(clear, surface))),
    )
}

@Composable
private fun ShowcasePoster(
    @DrawableRes resourceId: Int,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val ioDispatcher = LocalAppDispatchers.current.io
    val poster by produceState<ImageBitmap?>(
        initialValue = null,
        key1 = context,
        key2 = resourceId,
        key3 = ioDispatcher,
    ) {
        value = withContext(ioDispatcher) {
            context.decodePoster(resourceId)
        }
    }
    val image = poster
    Box(
        modifier = modifier.background(TaffyTheme.colors.surface),
        contentAlignment = Alignment.Center,
    ) {
        if (image != null) {
            Image(
                bitmap = image,
                contentDescription = null,
                contentScale = ContentScale.Fit,
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}

@Composable
internal fun rememberShowcaseLifecycleResumed(): Boolean {
    val owner = LocalLifecycleOwner.current
    var resumed by remember(owner) {
        mutableStateOf(owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED))
    }
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, _ ->
            resumed = owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }
    return resumed
}

@SuppressLint("ResourceType") // drawable-nodpi is intentionally decoded as an input stream.
private fun Context.decodePoster(@DrawableRes resourceId: Int): ImageBitmap? {
    if (resourceId == Resources.ID_NULL) return null
    return try {
        resources.openRawResource(resourceId).use { stream ->
            BitmapFactory.decodeStream(stream)?.asImageBitmap()
        }
    } catch (_: Resources.NotFoundException) {
        null
    }
}

private const val ShowcaseAspectRatio = 4f / 5f
private const val EdgeFeatherFraction = 0.04f
private val MaxEdgeFeather = 16.dp
