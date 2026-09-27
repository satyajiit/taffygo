// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskControl

/** User intents supported by the generated Core API task contract. */
sealed interface TaskViewIntent {
    data class Control(val control: TaskControl) : TaskViewIntent
    data object ApproveAction : TaskViewIntent
    data object DenyAction : TaskViewIntent
    data object RetryCore : TaskViewIntent
    data object SaveWorkspace : TaskViewIntent
    data object RequestDiscardWorkspace : TaskViewIntent
    data object CancelDiscardWorkspace : TaskViewIntent
    data object ConfirmDiscardWorkspace : TaskViewIntent

    /** The person read why a save or discard was refused and moved on. */
    data object DismissWorkspaceChangeRefusal : TaskViewIntent

    /** Put away the notice that a control was not taken. */
    data object DismissControlRefusal : TaskViewIntent

    /**
     * The person is done with a task that has ended, and wants the screen
     * back. Hides it here; the core keeps holding it (decision 0149).
     */
    data object PutTaskAway : TaskViewIntent
    data class AcceptArtifact(val artifactId: String) : TaskViewIntent
    data class ExportArtifact(
        val artifactId: String,
        val destination: ArtifactDestination,
    ) : TaskViewIntent
    data class ArtifactExportHandled(val requestId: String, val succeeded: Boolean) : TaskViewIntent
    data object RememberThis : TaskViewIntent
    data object DismissRememberThis : TaskViewIntent

    /** Not now, under a task that could not reach its provider: the reason stays, the offer goes. */
    data object DismissSetup : TaskViewIntent
    data object ReadAnswerAloud : TaskViewIntent
    data object StopReadAloud : TaskViewIntent

    enum class ArtifactDestination {
        CREATE_DOCUMENT,
        SHARE,
    }
}
