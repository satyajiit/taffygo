// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

/**
 * What a person can do with a form Taffy is holding open.
 *
 * Seven things, and one of them is deliberately absent: there is no Cancel.
 * Dismissing the sheet is [Dismiss] and it discards nothing — the task is still
 * waiting, what was typed is still here, and the pill puts it back. Giving up
 * on a task is Stop, which lives with the task's other controls and says what
 * it does.
 */
sealed interface TaskInputIntent {
    /** One row changed. Carries what is in the field, and never leaves this process. */
    data class ValueChanged(val fieldId: String, val value: String) : TaskInputIntent

    /** Move from editing to the exact-value confirmation. Sends nothing. */
    data object Submit : TaskInputIntent

    /** Approve once and send every exact row to the browser's vault. */
    data object Confirm : TaskInputIntent

    /** Return from confirmation to editing without sending anything. */
    data object Edit : TaskInputIntent

    /** Say the widget on the page has been dealt with. */
    data object CompleteInteractive : TaskInputIntent

    /** Put the sheet away without answering. Nothing is discarded. */
    data object Dismiss : TaskInputIntent

    /** Bring the dismissed sheet back. */
    data object Reopen : TaskInputIntent
}
