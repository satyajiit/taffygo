// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * Which set of tabs screen SCR-104 is showing.
 *
 * The two are a segmented control rather than a filter menu because they are a
 * property of the tab and not a view over one list: a private tab is not an
 * ordinary tab with a flag on it, and mixing the two in one grid is how a
 * private tab gets closed by accident or, worse, kept by accident.
 */
enum class TabSwitcherGroup {
    /** The tabs the user opened, which is where the switcher starts. */
    YOURS,

    /** The tabs that forget everything when they close. */
    PRIVATE,
}
