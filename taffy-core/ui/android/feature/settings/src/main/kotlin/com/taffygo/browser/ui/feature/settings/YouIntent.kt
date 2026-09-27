// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.LocalAvatar

/** Everything screen SCR-410 can be asked to do. */
sealed interface YouIntent {

    /** Open the profile details pane on this hub. */
    data object OpenDetails : YouIntent

    /** Close the details pane. A name typed and not yet stored survives it. */
    data object CloseDetails : YouIntent

    /** The name field changed. Nothing is written until [CommitName]. */
    data class EditName(val text: String) : YouIntent

    /** Write the typed name to this phone's profile. */
    data object CommitName : YouIntent

    /** Choose the face this phone's profile wears. */
    data class ChooseAvatar(val avatar: LocalAvatar) : YouIntent

    /** Open a hub child. */
    data class Open(val row: YouRow) : YouIntent
}
