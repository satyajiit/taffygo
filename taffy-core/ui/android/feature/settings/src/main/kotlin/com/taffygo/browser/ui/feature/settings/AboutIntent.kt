// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-408 can be asked to do. */
sealed interface AboutIntent {
    data object OpenHelp : AboutIntent

    /** The third-party notices this package carries (decision 0206). */
    data object OpenLicences : AboutIntent

    /** The public repository this build's source is published in (decision 0206). */
    data object OpenSource : AboutIntent
    data object Dismiss : AboutIntent
}
