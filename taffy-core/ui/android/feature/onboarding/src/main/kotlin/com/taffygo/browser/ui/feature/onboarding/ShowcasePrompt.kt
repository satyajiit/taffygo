// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyEaseOut
import com.taffygo.browser.ui.core.designsystem.taffyRunOnce
import com.taffygo.browser.ui.core.designsystem.taffySegment
import com.taffygo.browser.ui.core.ui.TaffyBrandMark

/**
 * The native prompt below a showcase film, including its one-shot typing motion.
 *
 * ## Why the card reserves a height nobody can see
 *
 * The visible text grows one character at a time, so left to itself the card
 * gains a line-height every time the string wraps. The pager above it is
 * `Modifier.weight(1f)`, so each of those jumps remeasures and resizes the
 * film — the shake. And the questions are not the same length, so reserving
 * only the current one still resizes the card at every auto-advance.
 *
 * So the text area is fixed at the tallest any of [questions] will ever be,
 * **measured** at the width it will actually be laid out in. It used to be
 * reserved by drawing the longest question transparently underneath, which is
 * a different quantity: longest by character count is not tallest once wrapped,
 * because a question one character shorter can still take an extra line. That
 * approximation held for the corpus it was written against and would have
 * broken the moment a question was reworded — and the test that guarded it
 * asserted the character property rather than the height.
 *
 * ## One clock, no animation-core
 *
 * The bubble's rise and the typing are two phases of one [taffyRunOnce] rather
 * than two `Animatable`s. `androidx.compose.animation.core` is not reachable
 * from the fork's GN graph at the pinned Chromium milestone — its visibility is
 * restricted to `//third_party/androidx:*` — so this module compiled only
 * because its GN target carried an `enable_bytecode_checks = false` waiver, and
 * OD-076 records that waiver as a debt to stop growing rather than to keep
 * paying. Every prior instance was answered by changing the downstream code.
 */
@Composable
internal fun ShowcasePrompt(
    question: String,
    questions: List<String>,
    animateTyping: Boolean,
) {
    val typingMillis = (question.length * TypingMsPerCharacter)
        .coerceIn(TypingMinMs, TypingMaxMs)
    val totalMillis = TypingLeadMs + typingMillis
    // Keyed on the question so a new slide restarts the run rather than
    // inheriting the previous one's position on the clock.
    val run by key(question) { taffyRunOnce(animateTyping, totalMillis) }
    val appear = if (animateTyping) {
        taffyEaseOut(taffySegment(run, 0f, BubbleAppearMs.toFloat() / totalMillis))
    } else {
        1f
    }
    val typed = if (animateTyping) {
        taffySegment(run, TypingLeadMs.toFloat() / totalMillis, 1f)
    } else {
        1f
    }
    val shown = (typed * question.length).toInt().coerceIn(0, question.length)
    val stillTyping = animateTyping && shown < question.length
    // The caret is a span on the character after the cursor rather than an
    // appended glyph: appending one changes the measured width and can push the
    // last word onto a new line by itself, which is the same defect one
    // character smaller.
    val spoken = buildAnnotatedString {
        append(question.take(shown))
        if (stillTyping) {
            withStyle(SpanStyle(color = TaffyTheme.colors.accent)) { append(CaretGlyph) }
        }
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .graphicsLayer {
                alpha = appear
                translationY = (1f - appear) * BubbleRisePx
            }
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card)
            .padding(TaffyTheme.spacing.snug)
            .testTag(SHOWCASE_QUESTION_TEST_TAG)
            .clearAndSetSemantics { contentDescription = question },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(QuestionMarkWell)
                .clip(TaffyTheme.shapes.pill)
                .background(TaffyTheme.colors.accentWash),
            contentAlignment = Alignment.Center,
        ) {
            TaffyBrandMark(size = QuestionMarkSize)
        }
        BoxWithConstraints(
            modifier = Modifier
                .weight(1f)
                .testTag(SHOWCASE_QUESTION_SIZER_TEST_TAG),
        ) {
            val style = TaffyTheme.typography.body
            val measurer = rememberTextMeasurer()
            val width = constraints.maxWidth
            val density = LocalDensity.current
            // Measured once per (corpus, width, type) rather than per frame:
            // the answer cannot change while all three hold still, and this
            // runs inside a pager that recomposes on every scroll pixel.
            val reserved = remember(questions, width, style, density) {
                val tallest = questions.maxOfOrNull { candidate ->
                    measurer.measure(
                        text = AnnotatedString(candidate),
                        style = style,
                        constraints = Constraints(maxWidth = width),
                    ).size.height
                } ?: 0
                with(density) { tallest.toDp() }
            }
            Text(
                text = spoken,
                style = style,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.height(reserved),
            )
        }
    }
}

internal const val SHOWCASE_QUESTION_TEST_TAG: String = "onboarding_showcase_question"

/** The reserved text area, for the test that asserts its height never moves. */
internal const val SHOWCASE_QUESTION_SIZER_TEST_TAG: String = "onboarding_showcase_question_sizer"

private val QuestionMarkWell = 32.dp
private val QuestionMarkSize = 22.dp
private const val CaretGlyph = "|"
private const val TypingLeadMs = 220
private const val TypingMsPerCharacter = 28
private const val TypingMinMs = 800
private const val TypingMaxMs = 2_400
private const val BubbleAppearMs = 280
private const val BubbleRisePx = 10f
