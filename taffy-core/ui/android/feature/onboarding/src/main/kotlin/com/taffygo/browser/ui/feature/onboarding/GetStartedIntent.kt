// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.LocalAvatar

/** Everything screen SCR-007 can be asked to do. */
sealed interface GetStartedIntent {

    /** The name field changed. Nothing is written until [Continue]. */
    data class EditName(val text: String) : GetStartedIntent

    /** Choose the face this phone's profile will wear. */
    data class ChooseAvatar(val avatar: LocalAvatar) : GetStartedIntent

    /** Open the sheet that says what this product does with a person's data. */
    data object OpenDataSheet : GetStartedIntent

    /** Close that sheet. */
    data object CloseDataSheet : GetStartedIntent

    /** Keep whatever was chosen and go on to AI setup. */
    data object Continue : GetStartedIntent
}
