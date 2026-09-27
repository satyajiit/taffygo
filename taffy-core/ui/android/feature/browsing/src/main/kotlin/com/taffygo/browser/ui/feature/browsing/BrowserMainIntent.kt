// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** Everything screen SCR-101 can be asked to do. */
sealed interface BrowserMainIntent {

    /** Open the address bar, focused. */
    data object FocusAddressBar : BrowserMainIntent

    /** Open the tab switcher. */
    data object OpenTabSwitcher : BrowserMainIntent

    /** Open the download list. */
    data object OpenDownloads : BrowserMainIntent

    /** Open settings. */
    data object OpenSettings : BrowserMainIntent

    /**
     * Open the workspace list.
     *
     * Two ways in from this screen: the start row carries its own workspaces
     * slot, and over a page the anchored menu carries the tile.
     */
    data object OpenWorkspaces : BrowserMainIntent

    /** Go back in the page's history, from the control in the action row. */
    data object GoBack : BrowserMainIntent

    /** Go forward in the page's history, from the control in the action row. */
    data object GoForward : BrowserMainIntent

    /**
     * The system back button or gesture, on the browser's primary surface.
     *
     * Not the same thing as [GoBack], which is a control that is only drawn
     * when there is page history and therefore only ever means one thing. This
     * one has two answers: the page before this one, and once the page has
     * nowhere left to go, the launcher — never the tab switcher, never a
     * start-page destination sitting under this one. Chrome's back stack is
     * the pages; chrome is not a history entry.
     */
    data object SystemBack : BrowserMainIntent

    /** Ask for the page again. */
    data object Reload : BrowserMainIntent

    /** Stop the selected page's current load. */
    data object StopLoading : BrowserMainIntent

    /** Open the top bar's anchored menu. */
    data object OpenMore : BrowserMainIntent

    /** Close the top bar's anchored menu. */
    data object DismissMore : BrowserMainIntent

    /** The person has read the notice about something that did not happen. */
    data object DismissNotice : BrowserMainIntent

    /**
     * Give the page back to the person, from the takeover band.
     *
     * Carried on this screen's own intent type so the band stays part of the
     * stateless surface a semantics test renders, and acted on by
     * [BrowserTakeoverViewModel] — the one that holds the task — rather than by
     * [BrowserMainViewModel], which holds the page.
     */
    data object TakeOver : BrowserMainIntent

    /** Open one of the frequent sites in this tab, by host. */
    data class OpenSite(val host: String) : BrowserMainIntent

    /** Ask again for the Python library the start page is waiting on. */
    data object RetryPageTools : BrowserMainIntent

    /** Open the site sheet (screen SCR-204), from the address pill's shield. */
    data object OpenSiteFiltering : BrowserMainIntent

    /** Close the site sheet. */
    data object DismissSiteFiltering : BrowserMainIntent

    /**
     * Block ads and trackers on the page's site, or stop blocking there.
     * Stopping records a site exception; blocking again removes it.
     */
    data class SetSiteBlocking(val blocked: Boolean) : BrowserMainIntent

    /** Open the blocking settings (screen SCR-206), from the site sheet. */
    data object OpenFilteringSettings : BrowserMainIntent

    /** Open find in page over the current tab. */
    data object OpenFindInPage : BrowserMainIntent

    /** Share the current page. */
    data object SharePage : BrowserMainIntent

    /** Review saved flows offered for this exact regular page. */
    data object OpenSavedFlows : BrowserMainIntent

    /** Open the save-page sheet. */
    data object OpenSavePage : BrowserMainIntent

    /** Close the save-page sheet. */
    data object DismissSavePage : BrowserMainIntent

    /** Open history. */
    data object OpenHistory : BrowserMainIntent

    /** Open bookmarks. */
    data object OpenBookmarks : BrowserMainIntent

    /** Open the library. */
    data object OpenLibrary : BrowserMainIntent

    /** Open You. */
    data object OpenYou : BrowserMainIntent

    /** Ask for the desktop version of this page. */
    data object ToggleDesktopSite : BrowserMainIntent

    /** Ask to review resetting permissions for the site named by the open sheet. */
    data object RequestSitePermissionReset : BrowserMainIntent

    /** Confirm the separately reviewed permission reset for the exact open site. */
    data object ConfirmSitePermissionReset : BrowserMainIntent

    /** Keep the site's changed permission choices. */
    data object DismissSitePermissionReset : BrowserMainIntent

    /** Make the selected page one Chromium preset larger. */
    data object ZoomPageIn : BrowserMainIntent

    /** Make the selected page one Chromium preset smaller. */
    data object ZoomPageOut : BrowserMainIntent

    /** Return the selected page to the profile's default zoom. */
    data object ResetPageZoom : BrowserMainIntent

    /** Close find in page. The query is forgotten. */
    data object DismissFindInPage : BrowserMainIntent

    /** The phrase in the find field changed. */
    data class FindQueryChanged(val query: String) : BrowserMainIntent

    /** Jump to the next find match. */
    data object FindNext : BrowserMainIntent

    /** Jump to the previous find match. */
    data object FindPrevious : BrowserMainIntent

    /** Star the page from the save-page sheet. */
    data object ConfirmSavePage : BrowserMainIntent

    /** Pick a folder on the save-page sheet. Empty means the default "All". */
    data class ChooseSaveFolder(val folderId: String) : BrowserMainIntent
}
