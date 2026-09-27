// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.ui.BackStack

/**
 * What the application shell renders: which screen, in which theme, in which
 * language and country, with which string transformation.
 *
 * The theme is the *preference*, not a resolved boolean: only the composition
 * knows what the system setting is, so resolving it here would mean reading a
 * device setting outside the tree that observes it. The language is the
 * preference too. The country constrains which language is valid, and
 * [preferencesLoaded] says whether both came from disk yet —
 * the platform applies a language by recreating the activity, so applying the
 * placeholder default on the way to the stored value would recreate it twice.
 */
data class ShellUiState(
    /** Where the user is, and everywhere they can go back to. */
    val backStack: BackStack = BackStack.start(),
    /** Which theme the user chose. */
    val theme: ThemePreference = ThemePreference.SYSTEM,
    /** Which language the user chose. */
    val appLanguage: AppLanguage = AppLanguage.SYSTEM,
    /** ISO country code constraining the languages the platform may expose. */
    val regionCode: String = "IN",
    /** Whether the preferences have been read from disk. */
    val preferencesLoaded: Boolean = false,
    /** Whether every string is shown pseudo-localized. */
    val pseudoLocalization: Boolean = false,
    /** Whether pages without a dark look should be drawn dark. */
    val forceDarkWeb: Boolean = false,
)
