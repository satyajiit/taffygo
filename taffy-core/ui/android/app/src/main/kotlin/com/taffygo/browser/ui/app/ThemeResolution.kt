// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.ThemePreference

/**
 * Whether the chosen preference means a dark tree, given the system setting.
 *
 * It is public because Chromium's browser-owned Compose shell resolves it
 * while projecting profile preferences into one Window. Keeping the decision
 * beside the portable model prevents platform hosts from inventing different
 * meanings for the same preference.
 *
 * It takes the system setting as a parameter rather than reading it, so it can
 * be answered from inside a composition with `isSystemInDarkTheme()` and from
 * outside one with the configuration's night mask, and the two readings cannot
 * drift.
 */
fun ThemePreference.isDark(systemInDarkTheme: Boolean): Boolean = when (this) {
    ThemePreference.SYSTEM -> systemInDarkTheme
    ThemePreference.LIGHT -> false
    ThemePreference.DARK -> true
}
