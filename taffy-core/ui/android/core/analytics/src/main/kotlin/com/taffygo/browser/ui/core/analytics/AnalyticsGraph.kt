// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.analytics

import com.taffygo.browser.ui.core.analytics.internal.InMemoryAnalyticsClient
import com.taffygo.browser.ui.core.common.Logger

/** Constructs the bounded diagnostic sink without widening its internal type. */
object AnalyticsGraph {

    /**
     * The seam a feature records through. This is the bounded
     * in-memory ring, which sends nothing anywhere.
     *
     * The logger arrives as a parameter because this module does not own it:
     * `:core:common` owns the sink, and a graph object that named
     * `android.util.Log` here would put a second one beside it.
     */
    fun analyticsClient(logger: Logger): AnalyticsClient = InMemoryAnalyticsClient(logger)
}
