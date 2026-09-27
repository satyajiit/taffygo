// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

import com.taffygo.browser.ui.core.common.internal.AndroidLogger
import com.taffygo.browser.ui.core.common.internal.DefaultAppDispatchers
import com.taffygo.browser.ui.core.common.internal.SystemClock

/** Constructs portable interfaces while keeping Android implementations internal. */
object CommonGraph {

    /** The dispatcher set every suspending call in the product goes through. */
    fun dispatchers(): AppDispatchers = DefaultAppDispatchers()

    /** The clock. Nothing reads `System.currentTimeMillis` directly. */
    fun clock(): Clock = SystemClock()

    /** The log sink, which carries no content by construction. */
    fun logger(): Logger = AndroidLogger()
}
