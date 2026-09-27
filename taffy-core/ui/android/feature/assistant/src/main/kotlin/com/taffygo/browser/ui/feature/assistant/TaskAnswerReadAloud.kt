// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskDisplayState

/**
 * Returns exactly the final answer text already visible on screen.
 *
 * Streaming text is deliberately ineligible: playback never follows a model
 * stream and never becomes a second route by which generated text leaves the
 * task surface. Truncated or incomplete final answers remain eligible, but
 * only their visible segments are spoken.
 */
internal fun readableTaskAnswer(state: TaskViewUiState): String? {
    if (!canReadTaskAnswer(state)) return null
    val answer = state.liveAnswer ?: return null
    return buildString(answer.segments.sumOf(String::length)) {
        answer.segments.forEach(::append)
    }.takeIf(String::isNotBlank)
}

/** Allocation-free eligibility check used while Compose projects controls. */
internal fun canReadTaskAnswer(state: TaskViewUiState): Boolean {
    if (state.state != TaskDisplayState.DONE && state.state != TaskDisplayState.PARTLY_DONE) {
        return false
    }
    val answer = state.liveAnswer ?: return false
    return !answer.isStreaming && answer.segments.any(String::isNotBlank)
}
