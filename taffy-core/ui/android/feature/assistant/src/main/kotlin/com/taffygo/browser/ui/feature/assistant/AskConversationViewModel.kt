// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModel
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import kotlinx.coroutines.flow.StateFlow

/**
 * The Ask overlay's window onto the followed task's conversation.
 *
 * It holds nothing of its own: the conversation is the repository's
 * residency (decision 0137), folded there from the answer stream and the
 * follow-ups sent, and this class exists so the panel reaches the repository
 * the way every surface does. [askConversation] is the projection, kept pure
 * so a host test can hold it.
 */
class AskConversationViewModel(
    tasks: TaskRepository,
) : ViewModel() {

    /** Every task the core holds, with the followed one's conversation on it. */
    val state: StateFlow<TaskRepositoryState> = tasks.state
}
