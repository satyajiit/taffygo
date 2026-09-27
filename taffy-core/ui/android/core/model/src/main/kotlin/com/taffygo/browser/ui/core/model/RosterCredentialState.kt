// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** The registry state of one stored provider credential. */
enum class RosterCredentialState {
    /** Present and expected to work. */
    USABLE,

    /** Present, but the person must sign in again before it can be used. */
    NEEDS_SIGN_IN,

    /** Present, and the last refresh attempt failed. */
    REFRESH_FAILED,
}
