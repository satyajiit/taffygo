// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** Everything screen SCR-102 can be asked to do. */
sealed interface NewTabIntent {

    /**
     * Somebody has put the caret in the box, for the first time on this screen.
     *
     * It opens a tab and goes nowhere. The box is typed into where it stands
     * (decision 0131), so there is no screen to change to — but the half of the
     * old behaviour that mattered is unchanged and happens at the same moment:
     * the address bar commits into whichever tab is *selected*, so a tab that
     * does not exist yet is a tab the typed address never reaches. See
     * [NewTabViewModel].
     */
    data object ComposerFocused : NewTabIntent

    /** Go to one of the frequent sites, by host. */
    data class OpenSite(val host: String) : NewTabIntent

    /**
     * The four chrome destinations in this screen's action row.
     *
     * They open no tab, and that is what separates them from everything above.
     * The intents above are all answers to "where does this new tab go"; these
     * four are the person leaving the question for a moment — to the tabs they
     * already have, their downloads, their workspaces, or settings — and
     * opening a tab on their behalf would leave an empty one behind every
     * time.
     */
    data object OpenTabSwitcher : NewTabIntent

    data object OpenDownloads : NewTabIntent

    data object OpenWorkspaces : NewTabIntent

    data object OpenSettings : NewTabIntent

    /**
     * Library, behind the plus on the box: the one place a person's own pages
     * are kept that the plus still opens. History and Bookmarks beside it are
     * attached to the request rather than opened (decision 0133), so they are
     * the box's own intents and not this screen's.
     *
     * It is here for the same reason the action row's four are, and behaves
     * the same way: no tab, and the chooser left standing underneath so back
     * returns to it.
     */
    data object OpenLibrary : NewTabIntent

    /** Ask again for the Python library the start page is waiting on. */
    data object RetryPageTools : NewTabIntent
}
