// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.lifecycle.ViewModel
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject

/** Screen SCR-002's one source of truth. */
class MeetTaffyViewModel @Inject constructor(
    private val analytics: AnalyticsClient,
) : ViewModel() {

    /** Act on something the user did. */
    fun onIntent(intent: MeetTaffyIntent, navigator: TaffyNavigator) {
        when (intent) {
            MeetTaffyIntent.Continue -> navigator.goTo(TaffyDestination.GetStarted)
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.MeetTaffy.screenId))
    }
}
