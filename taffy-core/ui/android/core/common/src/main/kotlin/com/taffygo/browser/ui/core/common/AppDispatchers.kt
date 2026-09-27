// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

import kotlinx.coroutines.CoroutineDispatcher

/**
 * The three dispatchers, injected rather than referenced (decision 0014
 * item 5). Nothing in TaffyGo-owned Kotlin names `Dispatchers.IO` directly:
 * a test that cannot replace a dispatcher is a test that cannot be
 * deterministic, and a screen that blocks the main thread is a defect the
 * architecture forbids by contract.
 */
interface AppDispatchers {
    /** The thread that renders. Nothing else runs here. */
    val main: CoroutineDispatcher

    /** Processor-bound work: parsing, reducing, formatting. */
    val default: CoroutineDispatcher

    /** Disk and network. */
    val io: CoroutineDispatcher
}
