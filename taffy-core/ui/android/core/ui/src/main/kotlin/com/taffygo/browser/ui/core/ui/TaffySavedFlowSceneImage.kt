// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The saved-flow plate: a decorative first-party illustration, and neither a
 * page preview nor a task result. Local, inert artwork stays outside the
 * content and action semantics of its parent.
 *
 * This was one arm of a two-scene enum until the start page took the other.
 * The browsing plate is still drawn — by [TaffyStartSceneImage], as the picture
 * that stands in until the delivered pack arrives — so one scene is left here,
 * and an enum of one is a branch that never goes the other way.
 */
@Composable
fun TaffySavedFlowSceneImage(modifier: Modifier = Modifier) {
    Image(
        painter = painterResource(R.drawable.taffy_scene_saved_flow),
        contentDescription = null,
        contentScale = ContentScale.Fit,
        modifier = modifier.aspectRatio(4f / 3f).clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.ribbonThreeWash)
            .testTag(TAFFY_SAVED_FLOW_SCENE_TEST_TAG),
    )
}

const val TAFFY_SAVED_FLOW_SCENE_TEST_TAG: String = "taffy_saved_flow_scene"
