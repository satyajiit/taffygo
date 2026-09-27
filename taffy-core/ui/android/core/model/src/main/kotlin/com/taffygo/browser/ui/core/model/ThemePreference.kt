// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The Appearance choice of screen SCR-407. The default follows the system
 * setting; light and dark are both first-class designs (screen catalog
 * section 1.2).
 */
enum class ThemePreference(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Follow the device setting. The default. */
    SYSTEM("system"),

    /** Always light. */
    LIGHT("light"),

    /** Always dark. */
    DARK("dark"),
}
