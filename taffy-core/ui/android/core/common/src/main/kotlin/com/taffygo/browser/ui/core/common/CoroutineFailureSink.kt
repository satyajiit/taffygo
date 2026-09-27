// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

/**
 * Content-free terminal sink for failures from supervised root coroutines.
 *
 * The throwable is deliberately absent: messages, stack-attached values, page
 * data, URLs, prompts, and credentials cannot cross this interface.
 */
fun interface CoroutineFailureSink {
    fun record(scope: CoroutineFailureScope)
}
