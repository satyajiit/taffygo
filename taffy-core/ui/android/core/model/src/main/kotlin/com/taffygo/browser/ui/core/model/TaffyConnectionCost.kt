// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** What the device's connection costs right now (screen SCR-203). */
enum class TaffyConnectionCost(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** There is no connection. */
    OFFLINE("offline"),

    /** Mobile data, or another connection paid for by the byte. */
    MOBILE_DATA("mobile-data"),

    /** Wi-Fi, or another connection that is not paid for by the byte. */
    WIFI("wifi"),
}
