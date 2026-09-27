// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import android.animation.ValueAnimator
import android.view.accessibility.AccessibilityManager
import androidx.compose.foundation.background
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerDefaults
import androidx.compose.foundation.pager.PagerState
import androidx.compose.foundation.pager.PagerSnapDistance
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first

/**
 * One full-width demonstration at a time.
 *
 * Pages fill the pager and the pager is clipped, so a neighbour is never
 * visible at rest. Auto-advance is a loop that waits for the pager to settle:
 * keying it on [androidx.compose.foundation.pager.PagerState.currentPage]
 * cancelled the scroll mid-flight and is what made the slide hitch.
 */
@Composable
internal fun TaffyShowcaseCarousel(
    slides: List<ShowcaseSlide>,
    soundEnabled: Boolean,
    modifier: Modifier = Modifier,
) {
    val lifecycleResumed = rememberShowcaseLifecycleResumed()
    val touchExplorationEnabled = rememberTouchExplorationEnabled()
    val motionAllowed = ValueAnimator.areAnimatorsEnabled() &&
        !TaffyTheme.reducedMotion &&
        !touchExplorationEnabled
    val motionActive = motionAllowed && lifecycleResumed
    val pagerState = rememberPagerState(pageCount = { slides.size })
    LaunchedEffect(motionActive, slides.size) {
        if (!motionActive || slides.size < 2) return@LaunchedEffect
        while (true) {
            val pageAtStart = pagerState.settledPage
            delay(SHOWCASE_CAROUSEL_DWELL_MS)
            snapshotFlow { pagerState.isScrollInProgress }.first { !it }
            // A manual swipe gets a full viewing interval of its own. Checking
            // after the delay avoids keying this effect on the page and
            // cancelling an auto-scroll while it is still travelling.
            if (pagerState.settledPage != pageAtStart) continue
            val next = (pagerState.settledPage + 1) % slides.size
            // The pager's own default spec. A `tween` here would come from
            // `androidx.compose.animation.core`, which the fork's GN graph does
            // not expose (OD-076); the default is a spring of a similar length
            // and this is a background auto-advance, not a signature motion.
            pagerState.animateScrollToPage(page = next)
        }
    }
    val settled = slides[pagerState.settledPage.coerceIn(0, slides.lastIndex)]

    BoxWithConstraints(
        modifier = modifier
            .fillMaxSize()
            .testTag(SHOWCASE_CAROUSEL_TEST_TAG),
    ) {
        val compact = usesCompactShowcaseLayout(maxHeight, LocalDensity.current.fontScale)
        if (compact) {
            CompactShowcase(
                slides = slides,
                settled = settled,
                pagerState = pagerState,
                motionAllowed = motionAllowed,
                motionActive = motionActive,
                soundEnabled = soundEnabled,
            )
        } else {
            PortraitShowcase(
                slides = slides,
                settled = settled,
                pagerState = pagerState,
                motionAllowed = motionAllowed,
                motionActive = motionActive,
                soundEnabled = soundEnabled,
            )
        }
    }
}

/**
 * Every question in the carousel, remembered so the list is stable.
 *
 * The prompt reserves its height from all of them rather than from the longest
 * by character count: longest is not tallest once a line wraps, and the two
 * differ by a whole line-height often enough to matter.
 */
@Composable
private fun allQuestions(slides: List<ShowcaseSlide>): List<String> =
    remember(slides) { slides.map { it.question } }

@Composable
private fun PortraitShowcase(
    slides: List<ShowcaseSlide>,
    settled: ShowcaseSlide,
    pagerState: PagerState,
    motionAllowed: Boolean,
    motionActive: Boolean,
    soundEnabled: Boolean,
) {
    Column(
        modifier = Modifier.fillMaxSize(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        ShowcasePager(
            slides = slides,
            pagerState = pagerState,
            motionAllowed = motionAllowed,
            motionActive = motionActive,
            showTitle = true,
            soundEnabled = soundEnabled,
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth(),
        )
        PageDots(count = slides.size, selected = pagerState.currentPage)
        ShowcasePrompt(
            question = settled.question,
            questions = allQuestions(slides),
            // `question` only changes once the pager settles, so gating on the
            // scroll as well only re-keyed the run when a flick sprang back —
            // retyping a question the person had already read.
            animateTyping = motionActive,
        )
    }
}

@Composable
private fun CompactShowcase(
    slides: List<ShowcaseSlide>,
    settled: ShowcaseSlide,
    pagerState: PagerState,
    motionAllowed: Boolean,
    motionActive: Boolean,
    soundEnabled: Boolean,
) {
    Row(
        modifier = Modifier.fillMaxSize(),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ShowcasePager(
            slides = slides,
            pagerState = pagerState,
            motionAllowed = motionAllowed,
            motionActive = motionActive,
            showTitle = false,
            soundEnabled = soundEnabled,
            modifier = Modifier
                .weight(1.1f)
                .fillMaxHeight(),
        )
        Column(
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = settled.title,
                style = TaffyTheme.typography.headline,
                color = TaffyTheme.colors.textPrimary,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag(SHOWCASE_TITLE_TEST_TAG),
            )
            PageDots(count = slides.size, selected = pagerState.currentPage)
            ShowcasePrompt(
                question = settled.question,
                questions = allQuestions(slides),
                animateTyping = motionActive,
            )
        }
    }
}

@Composable
private fun ShowcasePager(
    slides: List<ShowcaseSlide>,
    pagerState: PagerState,
    motionAllowed: Boolean,
    motionActive: Boolean,
    showTitle: Boolean,
    soundEnabled: Boolean,
    modifier: Modifier = Modifier,
) {
    HorizontalPager(
        state = pagerState,
        modifier = modifier.clipToBounds(),
        pageSpacing = 0.dp,
        beyondViewportPageCount = 1,
        userScrollEnabled = true,
        flingBehavior = PagerDefaults.flingBehavior(
            state = pagerState,
            pagerSnapDistance = PagerSnapDistance.atMost(1),
        ),
        verticalAlignment = Alignment.Top,
        key = { page -> slides[page].video },
    ) { page ->
        val slide = slides[page]
        Column(
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer { clip = true },
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (showTitle) {
                Text(
                    text = slide.title,
                    style = TaffyTheme.typography.display,
                    color = TaffyTheme.colors.textPrimary,
                    textAlign = TextAlign.Center,
                    modifier = Modifier
                        .fillMaxWidth()
                        .testTag(SHOWCASE_TITLE_TEST_TAG),
                )
            }
            // Motion-enabled neighbours wait on frame one. Reduced-motion
            // pages keep the committed result visible, while the settled page
            // may still decode invisibly when narration is enabled.
            ShowcaseVideoVisual(
                video = slide.video,
                videoDescription = slide.videoDescription,
                playing = !pagerState.isScrollInProgress &&
                    page == pagerState.settledPage,
                showFinalPoster = !motionAllowed,
                soundEnabled = soundEnabled,
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
            )
        }
    }
}

@Composable
private fun PageDots(count: Int, selected: Int) {
    Row(
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        repeat(count) { index ->
            val on = index == selected
            Box(
                modifier = Modifier
                    .size(if (on) DotOn else DotOff)
                    .clip(TaffyTheme.shapes.pill)
                    .background(
                        if (on) TaffyTheme.colors.textPrimary else TaffyTheme.colors.outline,
                    ),
            )
        }
    }
}

@Composable
private fun rememberTouchExplorationEnabled(): Boolean {
    val context = LocalContext.current
    val manager = remember(context) {
        context.getSystemService(AccessibilityManager::class.java)
    }
    var enabled by remember(manager) {
        mutableStateOf(manager?.isTouchExplorationEnabled == true)
    }
    DisposableEffect(manager) {
        if (manager == null) {
            onDispose {}
        } else {
            val listener = AccessibilityManager.TouchExplorationStateChangeListener { active ->
                enabled = active
            }
            manager.addTouchExplorationStateChangeListener(listener)
            onDispose { manager.removeTouchExplorationStateChangeListener(listener) }
        }
    }
    return enabled
}

internal fun usesCompactShowcaseLayout(availableHeight: Dp, fontScale: Float): Boolean =
    availableHeight < CompactHeight ||
        (fontScale >= LargeTextScale && availableHeight < LargeTextCompactHeight)

internal const val SHOWCASE_CAROUSEL_TEST_TAG: String = "onboarding_showcase_carousel"
internal const val SHOWCASE_TITLE_TEST_TAG: String = "onboarding_showcase_title"
internal const val SHOWCASE_VISUAL_TEST_TAG: String = "onboarding_showcase_visual"

private val DotOn = 6.dp
private val DotOff = 5.dp
internal const val SHOWCASE_CAROUSEL_DWELL_MS = 10_500L
private val CompactHeight = 360.dp
private val LargeTextCompactHeight = 520.dp
private const val LargeTextScale = 1.5f
