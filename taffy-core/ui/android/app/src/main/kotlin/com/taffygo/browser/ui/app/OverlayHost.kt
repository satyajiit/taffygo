// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.feature.assistant.AskConversationPanel
import com.taffygo.browser.ui.feature.browsing.AskOverlay

/**
 * One overlay destination, one overlay. The counterpart of [SinglePaneHost]
 * for the entries [com.taffygo.browser.ui.core.ui.ScreenPresentation] lifts
 * off the frame, composed by whichever host owns the screen beneath.
 *
 * Only the Ask overlay presents over its base today. A destination that says
 * it does and is not named here draws nothing, which is the honest failure:
 * the entry is still on the stack and back still pops it.
 *
 * The overlay is the browsing feature's — it is the start page's composer over
 * a page — and what Taffy answers with is the assistant feature's. Features
 * never depend on features, so the conversation reaches the overlay as a slot
 * filled here, the way the task panel reaches the start page.
 */
@Composable
internal fun OverlayHost(
    overlay: TaffyDestination,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    when (overlay) {
        is TaffyDestination.AssistantBar -> AskOverlay(
            destination = overlay,
            navigator = navigator,
            conversation = { started, tryAgain, leave ->
                AskConversationPanel(
                    navigator = navigator,
                    taskId = started.id,
                    goal = started.goal,
                    onTryAgain = tryAgain,
                    onLeave = leave,
                )
            },
            modifier = modifier,
        )
        else -> Unit
    }
}
