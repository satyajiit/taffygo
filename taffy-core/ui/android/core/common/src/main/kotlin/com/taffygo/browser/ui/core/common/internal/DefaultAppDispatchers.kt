// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common.internal

import com.taffygo.browser.ui.core.common.AppDispatchers
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers

/**
 * The one place `kotlinx.coroutines.Dispatchers` is named. Everything else
 * takes [AppDispatchers] by constructor, which is what makes every reducer and
 * every repository runnable on a laptop.
 */
@Singleton
internal class DefaultAppDispatchers @Inject constructor() : AppDispatchers {
    override val main: CoroutineDispatcher = Dispatchers.Main.immediate
    override val default: CoroutineDispatcher = Dispatchers.Default
    override val io: CoroutineDispatcher = Dispatchers.IO
}
