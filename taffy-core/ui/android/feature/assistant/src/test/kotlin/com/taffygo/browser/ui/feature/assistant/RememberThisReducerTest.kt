// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskDisplayState
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

/** Remember this? is one outline card on a finished task, dismissed locally. */
class RememberThisReducerTest {

    private val suggestion = RememberThisSuggestion(
        statement = "Prefers nonstop flights",
        why = "You corrected this twice in this workspace.",
    )
    private val finished = TaskViewUiState(
        state = TaskDisplayState.DONE,
        rememberThis = suggestion,
    )

    @Test
    fun `remember dismisses the card`() {
        assertNull(reduceTaskView(finished, TaskViewIntent.RememberThis).rememberThis)
    }

    @Test
    fun `not now dismisses the card`() {
        assertNull(reduceTaskView(finished, TaskViewIntent.DismissRememberThis).rememberThis)
    }

    @Test
    fun `a running task keeps other intents from clearing a suggestion`() {
        val running = finished.copy(state = TaskDisplayState.RUNNING)

        assertNotNull(reduceTaskView(running, TaskViewIntent.RetryCore).rememberThis)
    }
}
