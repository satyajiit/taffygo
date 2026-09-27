// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.ui.TaffyBrandLockup
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-002 — four ways Taffy works beside the current page. */
@Composable
fun MeetTaffyScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: MeetTaffyViewModel = screenViewModel(TaffyDestination.MeetTaffy)
    val context = LocalContext.current
    var soundEnabled by rememberShowcaseSoundEnabled {
        defaultShowcaseSoundEnabled(context)
    }

    LaunchedEffect(Unit) { viewModel.onShown() }

    MeetTaffyContent(
        onIntent = { viewModel.onIntent(it, navigator) },
        soundEnabled = soundEnabled,
        onSoundEnabledChange = { soundEnabled = it },
        modifier = modifier,
    )
}

@Composable
fun MeetTaffyContent(
    onIntent: (MeetTaffyIntent) -> Unit,
    soundEnabled: Boolean,
    onSoundEnabledChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    // Four slides, in the authored order of the seven that were rendered.
    // [ShowcaseVideo] states why the other three are not here.
    val slides = listOf(
        ShowcaseSlide(
            title = taffyString(R.string.taffy_showcase_form_title),
            question = taffyString(R.string.taffy_showcase_form_question),
            videoDescription = taffyString(R.string.taffy_showcase_form_video_description),
            video = ShowcaseVideo.FORM,
        ),
        ShowcaseSlide(
            title = taffyString(R.string.taffy_showcase_weekly_review_title),
            question = taffyString(R.string.taffy_showcase_weekly_review_question),
            videoDescription = taffyString(
                R.string.taffy_showcase_weekly_review_video_description,
            ),
            video = ShowcaseVideo.WEEKLY_REVIEW,
        ),
        ShowcaseSlide(
            title = taffyString(R.string.taffy_showcase_task_title),
            question = taffyString(R.string.taffy_showcase_task_question),
            videoDescription = taffyString(R.string.taffy_showcase_job_video_description),
            video = ShowcaseVideo.JOB,
        ),
        ShowcaseSlide(
            title = taffyString(R.string.taffy_showcase_page_tools_title),
            question = taffyString(R.string.taffy_showcase_page_tools_question),
            videoDescription = taffyString(
                R.string.taffy_showcase_page_tools_video_description,
            ),
            video = ShowcaseVideo.PAGE_TOOLS,
        ),
    )
    OnboardingSequence(
        destination = TaffyDestination.MeetTaffy,
        modifier = modifier,
        body = OnboardingBody.FILL,
        // The films bake the exact light/dark surface colour. A solid native
        // ground lets their outer pixels dissolve between the title and
        // prompt instead of masking moving backdrop waves as a rectangle.
        showBackdrop = false,
        footer = {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_onboarding_continue),
                onClick = { onIntent(MeetTaffyIntent.Continue) },
                modifier = Modifier.fillMaxWidth(),
                testTag = MEET_TAFFY_CONTINUE_TEST_TAG,
            )
        },
    ) {
        // The mark sits at the top, centred on the full width. The sound
        // control overlays the same box rather than sharing a row with it, so
        // the mark is centred on the screen and not on whatever is left over
        // once a 48dp button has taken its side. The box keeps the taller of
        // the two heights so the header cannot resize when the control
        // changes glyph.
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(HeaderHeight),
        ) {
            TaffyBrandLockup(
                height = TaffyLockupHeight,
                modifier = Modifier
                    .align(Alignment.Center)
                    .testTag(MEET_TAFFY_LOGO_TEST_TAG),
            )
            ShowcaseSoundButton(
                soundEnabled = soundEnabled,
                onSoundEnabledChange = onSoundEnabledChange,
                modifier = Modifier.align(Alignment.CenterEnd),
            )
        }
        TaffyShowcaseCarousel(
            slides = slides,
            soundEnabled = soundEnabled,
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .testTag(MEET_TAFFY_CAROUSEL_TEST_TAG),
        )
    }
}

const val MEET_TAFFY_LOGO_TEST_TAG: String = "meet_taffy_logo"
const val MEET_TAFFY_CAROUSEL_TEST_TAG: String = "meet_taffy_carousel"
const val MEET_TAFFY_CONTINUE_TEST_TAG: String = "meet_taffy_continue"

private val TaffyLockupHeight = 28.dp

/** The sound control's own touch target, which is the taller of the two. */
private val HeaderHeight = 48.dp
