// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import java.time.LocalDate
import java.time.LocalTime

/**
 * The start page's plate: a first-party painting chosen by the time of day.
 *
 * Decorative and inert. It carries no content description and no click action,
 * so it stays outside the semantics of the column it sits in — the same rule
 * [TaffyTaskSceneImage] follows, and for the same reason: this is weather, not
 * a page preview and not a result.
 *
 * ## What it draws before the pack arrives
 *
 * The delivered plates come through the delivery plane, which is a `required`
 * catalog row and therefore arrives on its own once a profile has a live
 * connection — behind the Python library, the filter list and the country
 * flags, because the plane runs one transfer at a time. Until then, and on any
 * device where it never completes, this draws `taffy_scene_browsing`, the plate
 * compiled into the installer. So the page is never empty and never waits, and
 * the picture simply gets better once the pack lands.
 *
 * ## Where the clock is read
 *
 * Here, once, and remembered for the life of the composition. The frame is a
 * new tab, and a new tab is the moment the question is asked; a plate that
 * changed under a person mid-look would read as a fault rather than as the hour
 * turning. [clock] and [today] exist so a preview or a test can state the moment
 * instead of depending on when it ran.
 *
 * [width] is taken rather than left to the caller's modifier because the source
 * needs it in pixels to decide how much of a 640-pixel painting to decode.
 */
@Composable
fun TaffyStartSceneImage(
    width: Dp,
    modifier: Modifier = Modifier,
    clock: () -> LocalTime = LocalTime::now,
    today: () -> LocalDate = LocalDate::now,
) {
    val scene = remember { StartSceneMember.sceneFor(clock().hour, today().dayOfYear) }
    val targetWidthPx = with(LocalDensity.current) { width.roundToPx() }
    val delivered = LocalStartSceneSource.current.sceneFor(scene, targetWidthPx)
    val frame = modifier
        .width(width)
        .aspectRatio(SceneAspectRatio)
        .clip(TaffyTheme.shapes.card)
        .background(TaffyTheme.colors.ribbonOneWash)
        .testTag(TAFFY_START_SCENE_TEST_TAG)
    when (delivered) {
        is StartSceneState.Ready -> Image(
            bitmap = delivered.artwork,
            contentDescription = null,
            contentScale = ContentScale.Fit,
            modifier = frame,
        )
        StartSceneState.Fallback -> Image(
            painter = painterResource(R.drawable.taffy_scene_browsing),
            contentDescription = null,
            contentScale = ContentScale.Fit,
            modifier = frame,
        )
    }
}

/**
 * The plate, which the start page's semantics tests name.
 *
 * One tag for both states on purpose: a test asking whether the start page has
 * artwork is asking about the slot, not about which pack answered it.
 */
const val TAFFY_START_SCENE_TEST_TAG: String = "taffy_start_scene"

private const val SceneAspectRatio = 4f / 3f
