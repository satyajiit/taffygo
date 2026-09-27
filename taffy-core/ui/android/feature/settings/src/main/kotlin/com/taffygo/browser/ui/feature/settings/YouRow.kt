// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.ui.TaffyDestination

/** The destinations the You hub lists, in order. */
enum class YouRow {
    PROFILE,
    SAVED_SIGN_INS,
    TIME_ON_SITES,
    MEMORY,
    SAVED_DETAILS,
    WHAT_HAPPENED,
    LIBRARY,
    ;

    /**
     * Where this row opens. Profile stays on You and opens the details pane;
     * the view-model reads that rather than pushing a second destination.
     */
    val destination: TaffyDestination
        get() = when (this) {
            PROFILE -> TaffyDestination.You
            TIME_ON_SITES -> TaffyDestination.TimeOnSites
            MEMORY -> TaffyDestination.Memory
            SAVED_SIGN_INS -> TaffyDestination.SavedSignIns
            SAVED_DETAILS -> TaffyDestination.SavedDetails
            WHAT_HAPPENED -> TaffyDestination.WhatHappened
            LIBRARY -> TaffyDestination.LibraryHome
        }
}
