// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

/** Typed refusal from the browser-owned command admission boundary. */
class CoreApiSubmissionException(
    val reason: Reason,
) : Exception() {
    /** Closed failures a UI command can observe before immutable state changes. */
    enum class Reason {
        INVALID_REQUEST,
        STALE_GENERATION,
        STALE_REVISION,
        DEADLINE_EXCEEDED,
        BACKPRESSURE,
        CORE_UNAVAILABLE,
        DUPLICATE,
        PROTOCOL_VIOLATION,

        // A readable start refused because of the person's tabs or windows
        // (decision 0231): no rewording of the request fixes any of these.

        /** No tab of the person's own shows a site the start names. */
        SOURCE_NOT_OPEN,

        /** More than one of the person's own tabs shows a site the start names. */
        SOURCE_AMBIGUOUS,

        /** There is not exactly one window in front that can hold Taffy's tabs. */
        WINDOW_UNAVAILABLE,
    }
}
