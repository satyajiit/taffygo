// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.ThemePreference

/** Everything screen SCR-001 can be asked to do. */
sealed interface WelcomeIntent {

    /**
     * The one forward action. The screen offers nothing else on purpose: the
     * catalog's SCR-001 row is a promise and a way in, with no account and no
     * setup between the user and a browser.
     */
    data object StartBrowsing : WelcomeIntent

    /**
     * Change the language before reading any of this. The chooser is a side
     * door off the sequence, so it carries no step and back returns here.
     */
    data object OpenLanguageRegion : WelcomeIntent

    /** Choose the appearance without leaving the Welcome screen. */
    data class ChooseTheme(val theme: ThemePreference) : WelcomeIntent
}
