// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent

/** Records screen-shown events and nothing else. */
class PagesTestAnalytics : AnalyticsClient {
    val events = mutableListOf<AnalyticsEvent>()

    override fun record(event: AnalyticsEvent) {
        events += event
    }

    override fun recent(): List<AnalyticsEvent> = events
}
