// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.analytics.internal

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.LogLevel
import com.taffygo.browser.ui.core.common.Logger
import javax.inject.Inject
import javax.inject.Singleton

/**
 * The UI layer recorder: a bounded ring of the most recent events, and a line in
 * the diagnostic log.
 *
 * Nothing leaves the device. There is no allowlist check here, and there does
 * not need to be one: [AnalyticsEvent] is a sealed hierarchy that no other
 * module can extend, so an event with an undeclared name cannot be constructed
 * to be refused. The allowlist is asserted against the hierarchy by
 * `AnalyticsPayloadTest` instead, which is where a new undeclared event would
 * actually be caught.
 */
@Singleton
internal class InMemoryAnalyticsClient @Inject constructor(
    private val logger: Logger,
) : AnalyticsClient {

    private val events = ArrayDeque<AnalyticsEvent>()

    override fun record(event: AnalyticsEvent) {
        synchronized(events) {
            events.addLast(event)
            while (events.size > CAPACITY) events.removeFirst()
        }
        logger.log(LogLevel.DEBUG, TAG, "${event.name} ${event.parameters}")
    }

    override fun recent(): List<AnalyticsEvent> = synchronized(events) { events.toList() }

    private companion object {
        const val TAG = "TaffyAnalytics"

        /** How many events the UI layer keeps for its own diagnostics. */
        const val CAPACITY = 100
    }
}
