// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.analytics

/**
 * Where a typed event goes.
 *
 * Collection is switched off until the consent decision `[Open (OD-048)]` is
 * made, and no provider is wired into the UI layer at all: decision 0014 names
 * Firebase, and adding it before the consent model exists would be shipping the
 * collection ahead of the decision that governs it. So the UI layer records
 * events in memory, where [recent] can show them and a test can assert what
 * they contain.
 */
interface AnalyticsClient {

    /** Record one event. Never blocks, and never leaves the device. */
    fun record(event: AnalyticsEvent)

    /** The most recent events, newest last, for the UI layer's own diagnostics. */
    fun recent(): List<AnalyticsEvent>
}
