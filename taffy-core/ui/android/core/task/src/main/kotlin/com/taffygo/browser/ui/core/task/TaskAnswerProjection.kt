// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

/** Process-resident visible answer text for one task. */
data class TaskAnswerProjection(
    /**
     * Sanitized text emitted by the isolated core, never provider framing.
     *
     * The pieces are one logical string and must be read in order. Keeping
     * sealed pieces separate lets a streaming update retain the already
     * published prefix instead of copying and laying out the whole answer on
     * every UI frame.
     */
    val segments: List<String>,
    /** Whether the latest model call is still emitting. */
    val isStreaming: Boolean,
    /** Whether a missing or reordered event made the text non-contiguous. */
    val isIncomplete: Boolean,
    /** Whether the process-residency byte ceiling stopped further text. */
    val isTruncated: Boolean,
)
