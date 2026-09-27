// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

/**
 * A total result type. Every boundary in the UI layer returns one of these
 * rather than throwing, so a caller cannot forget the failure path and a
 * failure carries a typed reason instead of a message a screen would have to
 * parse.
 */
sealed interface TaffyResult<out T> {

    /** The call produced a value. */
    data class Success<T>(val value: T) : TaffyResult<T>

    /** The call did not, and says why in a closed vocabulary. */
    data class Failure(val reason: FailureReason) : TaffyResult<Nothing>

    /** The value, or null when this is a failure. */
    fun valueOrNull(): T? = (this as? Success)?.value

    /** The reason, or null when this is a success. */
    fun reasonOrNull(): FailureReason? = (this as? Failure)?.reason
}
