// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.taffyDwellEase
import com.taffygo.browser.ui.core.designsystem.taffyPingPongPhase

/**
 * One line of text that travels when it does not fit, so the whole of it can be
 * read.
 *
 * The assistant pill is the reason this exists. Its line is the product's one
 * statement of what Taffy is doing, and on a phone the row around it leaves it
 * about 160 density-independent pixels — enough for the design document's own
 * fixed copy and not for the sentences the core actually sends, so half of
 * "Taffy's last move was refused. Trying another way…" was an ellipsis. An
 * ellipsis on the only line that says what is happening is the one place in
 * this product where the words matter more than the composure.
 *
 * It travels **out and back** rather than looping. A loop needs a gap and a
 * seam, and a seam in a sentence reads as a second sentence starting; out and
 * back has neither. It **stops at each end** rather than easing through the
 * turn: the far end of a sentence is the part somebody waited for, and a turn
 * that merely decelerates through it gives them no moment to read it
 * ([taffyDwellEase]). It travels at a reading pace and not a ticker's. Nothing
 * moves at all when the text fits, and the animation is not merely paused then —
 * [taffyPingPongPhase] schedules no frame when it is not running, so a still
 * line costs nothing.
 *
 * **The crop is a fade, not a cut.** A sentence sliced mid-glyph at a hard edge
 * reads as a rendering fault; a few density-independent pixels of fade reads as
 * more text. Each edge fades in proportion to what is actually beyond it, so the
 * first word is sharp until the line starts to travel and the last word is sharp
 * once it arrives.
 *
 * **Reduced motion keeps the ellipsis.** A person who asked for stillness is
 * not asking to read a sentence by watching it move, and the full line is
 * available to them where every line is: in the task view the pill opens.
 */
@Composable
internal fun TaffyMarqueeText(
    text: String,
    style: TextStyle,
    color: Color,
    reducedMotion: Boolean,
    modifier: Modifier = Modifier,
    textModifier: Modifier = Modifier,
) {
    BoxWithConstraints(modifier = modifier) {
        val room = constraints.maxWidth
        // Reset with the sentence: a line that has just been replaced is not
        // the line whose width was measured, and a stale overflow would send a
        // short sentence travelling off its own pill for one frame.
        var drawn by remember(text) { mutableIntStateOf(0) }
        val overflow = (drawn - room).coerceAtLeast(0)
        val travels = !reducedMotion && overflow > 0
        val fade = with(LocalDensity.current) { FadeWidth.toPx() }
        val phase = taffyPingPongPhase(
            running = travels,
            periodMillis = marqueeLegMillis(with(LocalDensity.current) { overflow.toDp() }),
        )
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clipToBounds()
                // The mask subtracts from what this box drew, so the box has to
                // be its own layer; without the offscreen strategy `DstIn` would
                // be composited against whatever is behind the pill instead.
                .graphicsLayer { compositingStrategy = CompositingStrategy.Offscreen }
                .drawWithContent {
                    drawContent()
                    if (!travels) return@drawWithContent
                    // Read in the draw pass rather than in composition, so a
                    // travelling line re-masks itself without recomposing the
                    // pill around it on every frame.
                    val gone = overflow * taffyDwellEase(phase.value, MarqueeDwell)
                    fadeEdges(behind = gone, ahead = overflow - gone, over = fade)
                },
        ) {
            Text(
                text = text,
                style = style,
                color = color,
                maxLines = 1,
                // Laid out at the width the sentence wants rather than the width
                // there is, which is what makes the overflow measurable at all;
                // the box above clips it back to the room it has.
                softWrap = false,
                overflow = if (reducedMotion) TextOverflow.Ellipsis else TextOverflow.Clip,
                onTextLayout = { drawn = it.size.width },
                modifier = Modifier
                    .then(
                        if (reducedMotion) {
                            Modifier
                        } else {
                            Modifier.wrapContentWidth(Alignment.Start, unbounded = true)
                        },
                    )
                    .graphicsLayer {
                        translationX = -overflow * taffyDwellEase(phase.value, MarqueeDwell)
                    }
                    .then(textModifier),
            )
        }
    }
}

/**
 * The two soft edges, each as wide as there is something behind it to suggest.
 *
 * Proportional rather than constant so the fade is never a lie: an edge with
 * nothing past it is not softened, which is what keeps the first word crisp
 * before the line moves and the last word crisp when it lands.
 */
private fun DrawScope.fadeEdges(behind: Float, ahead: Float, over: Float) {
    val start = over * (behind / over).coerceIn(0f, 1f)
    val end = over * (ahead / over).coerceIn(0f, 1f)
    if (start > 0f) {
        drawRect(
            brush = Brush.horizontalGradient(
                listOf(Color.Transparent, Color.Black),
                startX = 0f,
                endX = start,
            ),
            size = Size(start, size.height),
            blendMode = BlendMode.DstIn,
        )
    }
    if (end > 0f) {
        drawRect(
            brush = Brush.horizontalGradient(
                listOf(Color.Black, Color.Transparent),
                startX = size.width - end,
                endX = size.width,
            ),
            topLeft = Offset(size.width - end, 0f),
            size = Size(end, size.height),
            blendMode = BlendMode.DstIn,
        )
    }
}

/**
 * How long one leg of the journey takes, from how far there is to go.
 *
 * Proportional, so a sentence that is barely too long does not crawl and a very
 * long one does not sprint, and bounded at both ends because neither extreme
 * reads as a line being offered to be read.
 */
private fun marqueeLegMillis(distance: Dp): Int =
    (distance / MarqueeSpeed * 1000f).toInt().coerceIn(MarqueeLegMinMs, MarqueeLegMaxMs)

/** Reading pace rather than ticker pace: an unhurried 22 dp a second. */
private val MarqueeSpeed = 22.dp
private const val MarqueeLegMinMs = 2600
private const val MarqueeLegMaxMs = 11000

/**
 * The share of each leg spent stationary at that end.
 *
 * A fifth at each end, so the far end of the journey is two of them back to
 * back — a second or more on a typical line, which is time to read the words
 * that just arrived before they leave again.
 */
private const val MarqueeDwell = 0.2f

/** How much of each edge the crop fades over. */
private val FadeWidth = 18.dp
