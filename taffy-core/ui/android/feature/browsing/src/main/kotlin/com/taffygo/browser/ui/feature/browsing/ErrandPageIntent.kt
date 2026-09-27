// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** What a person can do on screen SCR-110, which is two things and no more. */
sealed interface ErrandPageIntent {

    /**
     * Go back.
     *
     * The vendor's own pages first — an account chooser and the consent screen
     * after it are two pages, and a person who picked the wrong account has to
     * be able to return to it — and the errand itself only once there is
     * nothing left to walk.
     */
    data object Back : ErrandPageIntent

    /** Leave the errand now, whatever page it is on. */
    data object Close : ErrandPageIntent
}
