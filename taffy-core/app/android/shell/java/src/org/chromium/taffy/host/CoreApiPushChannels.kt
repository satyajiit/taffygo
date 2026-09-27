// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.api.AssetProgressReport
import com.taffygo.browser.ui.core.api.ComposerCompletionReport
import com.taffygo.browser.ui.core.api.TaskAnswerReport
import com.taffygo.browser.ui.core.api.TaskArtifactExportReport
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow

/**
 * The four things the browser pushes rather than publishes.
 *
 * A fourth collaborator beside the snapshot ledger, the submission ledger and
 * the permission dispatcher, holding the one channel none of those describe:
 * values too short-lived to be state. Neither is validated and neither is
 * remembered — both interfaces are closed, every field is a count the browser
 * measured or a string a provider answered, and the snapshot is what says
 * anything that is true.
 *
 * They differ in one way, which is why they are worth reading side by side.
 * Progress replays and a suggestion does not: a screen that arrives mid
 * download should see where the download got to, while a composer that arrives
 * after an answer must not be handed a suggestion for text the person has since
 * typed past.
 */
internal class CoreApiPushChannels {
    // Dropping the oldest rather than suspending, because the producer is
    // Chromium's UI thread and a byte count nobody collected fast enough is a
    // figure that is already out of date. The snapshot carries where the
    // download really got to, so nothing is lost by missing one of these.
    private val assetProgressSink = MutableSharedFlow<AssetProgressReport>(
        replay = 1,
        extraBufferCapacity = ASSET_PROGRESS_BUFFER,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )

    // No replay, deliberately. A suggestion is offered to the request that
    // asked for it, and a collector subscribing later asked for nothing; a
    // replayed one would arrive as ghost text over a sentence the person has
    // already finished.
    private val composerCompletionSink = MutableSharedFlow<ComposerCompletionReport>(
        replay = 0,
        extraBufferCapacity = COMPOSER_COMPLETION_BUFFER,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )

    // Collected eagerly by the profile task repository, with no replay: a
    // partial answer belongs to the live process and a late collector must not
    // splice itself into the middle of a model call. Sequence numbers let the
    // repository reject a dropped or reordered burst instead of concatenating
    // a sentence that was never sent.
    private val taskAnswerSink = MutableSharedFlow<TaskAnswerReport>(
        replay = 0,
        extraBufferCapacity = TASK_ANSWER_BUFFER,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )

    // No replay and bounded more tightly than text: each event can hold the
    // contract's complete file allowance, and a superseded save request can
    // safely ask again under a new identity.
    private val taskArtifactExportSink = MutableSharedFlow<TaskArtifactExportReport>(
        replay = 0,
        extraBufferCapacity = TASK_ARTIFACT_EXPORT_BUFFER,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )

    val assetProgress: Flow<AssetProgressReport> = assetProgressSink.asSharedFlow()

    val composerCompletion: Flow<ComposerCompletionReport> =
        composerCompletionSink.asSharedFlow()

    val taskAnswer: Flow<TaskAnswerReport> = taskAnswerSink.asSharedFlow()

    val taskArtifactExport: Flow<TaskArtifactExportReport> =
        taskArtifactExportSink.asSharedFlow()

    fun push(report: AssetProgressReport) {
        assetProgressSink.tryEmit(report)
    }

    fun push(report: ComposerCompletionReport) {
        composerCompletionSink.tryEmit(report)
    }

    fun push(report: TaskAnswerReport) {
        taskAnswerSink.tryEmit(report)
    }

    fun push(report: TaskArtifactExportReport) {
        taskArtifactExportSink.tryEmit(report)
    }
}

// Room for a burst while the screen is off the front. Small on purpose:
// beyond a couple of readings, the older ones are only history.
private const val ASSET_PROGRESS_BUFFER = 8

// Exactly one request is in flight (decision 0097 section 3), so this is room
// for the answer to a request the surface has already superseded rather than
// for a queue. The report carries its own identity, and a superseded one is
// dropped by whoever collects it.
private const val COMPOSER_COMPLETION_BUFFER = 2

// The network transport is backpressured at the sandbox seam, so this is not
// a response-body buffer. It only covers a short UI-thread burst while the
// eager repository collector is scheduled.
private const val TASK_ANSWER_BUFFER = 64

// At most four complete 256 KiB files can wait for the trusted document
// writer. Older events are tied to older explicit request identities.
private const val TASK_ARTIFACT_EXPORT_BUFFER = 4
