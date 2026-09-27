// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.analytics

/**
 * The whole analytics vocabulary (android-app-architecture section 8).
 *
 * Feature code cannot log a free-form string, because there is no member here
 * that carries one. Every event name and every parameter value is a
 * compiled-in token: a screen identifier, an enumeration label, or a coarse
 * bucket. No URL, title, prompt, output, file name, or identifier beyond the
 * allowlist can reach a payload, and `AnalyticsPayloadTest` asserts that over
 * every event rather than over the ones someone remembered.
 */
sealed interface AnalyticsEvent {

    /** The event name, from a closed set. */
    val name: String

    /** The parameters, all of them compiled-in tokens. */
    val parameters: Map<String, String>

    /** A screen was shown. Carries the catalog identifier, never the content. */
    data class ScreenShown(val screenId: String) : AnalyticsEvent {
        override val name: String = "screen_shown"
        override val parameters: Map<String, String> = mapOf("screen_id" to screenId)
    }

    /** A task reached a user-visible state. */
    data class TaskStateChanged(val stateLabel: String) : AnalyticsEvent {
        override val name: String = "task_state_changed"
        override val parameters: Map<String, String> = mapOf("state" to stateLabel)
    }

    /** The user used one of the three controls. */
    data class TaskControlUsed(val controlLabel: String) : AnalyticsEvent {
        override val name: String = "task_control_used"
        override val parameters: Map<String, String> = mapOf("control" to controlLabel)
    }

    /** A task finished, with how many sources it rested on, in a bucket. */
    data class TaskFinished(val stateLabel: String, val sourceCountBucket: String) : AnalyticsEvent {
        override val name: String = "task_finished"
        override val parameters: Map<String, String> =
            mapOf("state" to stateLabel, "sources" to sourceCountBucket)
    }

    /** An export was written, named by format only. */
    data class ArtifactExported(val formatLabel: String) : AnalyticsEvent {
        override val name: String = "artifact_exported"
        override val parameters: Map<String, String> = mapOf("format" to formatLabel)
    }

    /** A page snapshot was inspected, named by its closed UI definition. */
    data class SnapshotInspected(val definition: String) : AnalyticsEvent {
        override val name: String = "snapshot_inspected"
        override val parameters: Map<String, String> = mapOf("definition" to definition)
    }

    /** Something failed, named by its closed reason. */
    data class FailureObserved(val reasonLabel: String) : AnalyticsEvent {
        override val name: String = "failure_observed"
        override val parameters: Map<String, String> = mapOf("reason" to reasonLabel)
    }

    companion object {
        /** Every event name this build may emit. */
        val ALLOWED_NAMES: Set<String> = setOf(
            "screen_shown",
            "task_state_changed",
            "task_control_used",
            "task_finished",
            "artifact_exported",
            "snapshot_inspected",
            "failure_observed",
        )
    }
}
