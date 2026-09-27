// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import android.animation.ValueAnimator
import androidx.compose.foundation.layout.Spacer
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyPhase
import kotlin.math.PI
import kotlin.math.sin

/**
 * Quiet waves behind every first-run page.
 *
 * They carry no meaning and no accent: the motion is a darker (or, on paper,
 * a slightly recessed) step of the same surface, so body text keeps the
 * contrast the theme already asserts.
 *
 * The phase is a modulo loop ([taffyPhase]), and each wave's speed is an
 * integer number of periods per loop, so the shape at 1 is the shape at 0
 * and the band never restarts.
 */
@Composable
internal fun OnboardingBackdrop(modifier: Modifier = Modifier) {
    val motionEnabled = ValueAnimator.areAnimatorsEnabled() && !TaffyTheme.reducedMotion
    // Deliberately not `by`. Reading the clock here would invalidate this
    // composable's recompose scope every frame for a value only the draw uses;
    // read inside the draw and only the draw is invalidated.
    val phase = taffyPhase(running = motionEnabled, periodMillis = WavePeriodMs)
    val dark = TaffyTheme.isDark
    val waves = rememberBackdropWaves(dark)
    val path = remember { Path() }
    Spacer(
        // The brushes are built once per size rather than once per frame. A
        // `ShaderBrush` caches its native shader on the instance, so building
        // three of them inside the draw allocated three `LinearGradient`s a
        // frame and never hit the cache once. This backdrop is composed behind
        // the whole first-run sequence, including while a sheet is sliding, so
        // that cost was being paid exactly when there was least to spare.
        modifier = modifier.drawWithCache {
            val brushes = waves.map { wave ->
                Brush.verticalGradient(
                    colors = listOf(wave.color, Color.Transparent),
                    startY = size.height * wave.anchorY - size.height * wave.amplitude,
                    endY = size.height,
                )
            }
            onDrawBehind {
                val travel = if (motionEnabled) phase.value else StaticPhase
                waves.forEachIndexed { index, wave ->
                    path.rewind()
                    appendWave(
                        path = path,
                        phase = travel * wave.speed + wave.shift,
                        yCenter = size.height * wave.anchorY,
                        amplitude = size.height * wave.amplitude,
                        cycles = wave.cycles,
                    )
                    drawPath(path = path, brush = brushes[index])
                }
            }
        },
    )
}

@Composable
private fun rememberBackdropWaves(dark: Boolean): List<BackdropWave> {
    val raised = TaffyTheme.colors.surfaceRaised
    val sunken = TaffyTheme.colors.surfaceSunken
    val outline = TaffyTheme.colors.outline
    val hairline = TaffyTheme.colors.hairline
    return remember(dark, raised, sunken, outline, hairline) {
        if (dark) {
            listOf(
                BackdropWave(raised.copy(alpha = DarkRaisedAlpha), 0.50f, 0.030f, 0.80f, 0.00f, 1f),
                BackdropWave(outline.copy(alpha = DarkOutlineAlpha), 0.64f, 0.022f, 1.10f, 0.33f, 1f),
                BackdropWave(hairline.copy(alpha = DarkHairlineAlpha), 0.76f, 0.014f, 1.55f, 0.67f, 2f),
            )
        } else {
            listOf(
                BackdropWave(sunken.copy(alpha = LightSunkenAlpha), 0.52f, 0.024f, 0.75f, 0.00f, 1f),
                BackdropWave(outline.copy(alpha = LightOutlineAlpha), 0.66f, 0.016f, 1.05f, 0.37f, 1f),
                BackdropWave(hairline.copy(alpha = LightHairlineAlpha), 0.78f, 0.010f, 1.40f, 0.62f, 2f),
            )
        }
    }
}

private fun DrawScope.appendWave(
    path: Path,
    phase: Float,
    yCenter: Float,
    amplitude: Float,
    cycles: Float,
) {
    val width = size.width
    val height = size.height
    path.moveTo(0f, height)
    path.lineTo(0f, yAt(0f, phase, yCenter, amplitude, cycles))
    var i = 1
    while (i <= WaveSegments) {
        val t = i / WaveSegments.toFloat()
        path.lineTo(width * t, yAt(t, phase, yCenter, amplitude, cycles))
        i += 1
    }
    path.lineTo(width, height)
    path.close()
}

private fun yAt(t: Float, phase: Float, yCenter: Float, amplitude: Float, cycles: Float): Float =
    yCenter + sin((t * cycles + phase) * TwoPi) * amplitude

private data class BackdropWave(
    val color: Color,
    val anchorY: Float,
    val amplitude: Float,
    val cycles: Float,
    val shift: Float,
    val speed: Float,
)

private const val TwoPi = (2f * PI).toFloat()
private const val WavePeriodMs = 28_000
private const val WaveSegments = 64
private const val StaticPhase = 0.18f
private const val DarkRaisedAlpha = 0.28f
private const val DarkOutlineAlpha = 0.18f
private const val DarkHairlineAlpha = 0.10f
private const val LightSunkenAlpha = 0.30f
private const val LightOutlineAlpha = 0.20f
private const val LightHairlineAlpha = 0.10f
