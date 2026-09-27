// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.internal

import com.taffygo.browser.ui.core.common.CoroutineFailureScope
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import java.util.concurrent.CancellationException
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineExceptionHandler

/** Builds the terminal handler shared by every Chromium-owned lifetime. */
internal object CoroutineFailureHandlers {
    fun create(
        scope: CoroutineFailureScope,
        sink: CoroutineFailureSink,
    ): CoroutineContext = CoroutineExceptionHandler { _, failure ->
        if (failure !is CancellationException) sink.record(scope)
    }
}
