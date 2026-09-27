// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.internal

import com.taffygo.browser.ui.core.common.Clock
import javax.inject.Inject
import javax.inject.Singleton

/** The one place the wall clock is read. */
@Singleton
internal class SystemClock @Inject constructor() : Clock {
    override fun nowEpochMillis(): Long = System.currentTimeMillis()
}
