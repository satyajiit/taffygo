// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.ThemePreference

/**
 * Screen SCR-001 — Welcome.
 *
 * Taffy is a product name rather than a preference. Locale and appearance are
 * the two useful choices someone can make before reading the rest of the flow.
 */
data class WelcomeUiState(
    /** Which language the interface is drawn in, for the chip in the corner. */
    val appLanguage: AppLanguage = AppLanguage.SYSTEM,
    /** ISO country code shown beside the language. */
    val regionCode: String = "IN",
    /** Explicit appearance choice, or the system choice before first use. */
    val theme: ThemePreference = ThemePreference.SYSTEM,
)
