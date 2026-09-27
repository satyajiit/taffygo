// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

/**
 * The projection from browser truth to what screen SCR-101 renders.
 *
 * It is a pure function of the flows the screen watches, which is what makes
 * every state of the screen — including the ones that need a page to have
 * failed — a plain unit test. The overflow sheet is the one input that is not
 * browser truth: it is what the person did to the chrome, so it arrives
 * beside the truth rather than being read back out of the view.
 */
internal fun projectBrowserMain(
    navigation: NavigationState,
    tabs: List<Tab>,
    moreOpen: Boolean = false,
    notice: BrowserNotice? = null,
    appearance: PageAppearance = PageAppearance(),
    startPageGate: StartPageGate = StartPageGate(),
    sites: List<FrequentSite> = emptyList(),
    artwork: Map<TabId, TabArtwork> = emptyMap(),
    siteMarks: Map<String, Bitmap> = emptyMap(),
    filtering: FilteringSettings = FilteringSettings(),
    siteFilteringOpen: Boolean = false,
    findInPage: FindInPageUiState = FindInPageUiState(),
    savePageOpen: Boolean = false,
    bookmarksWritable: Boolean = false,
    savePageStatus: SavePageUiState.SaveStatus = SavePageUiState.SaveStatus.IDLE,
    desktopSite: Boolean = false,
    desktopSiteAvailable: Boolean = false,
    permissions: SiteInfoRepository.PermissionState =
        SiteInfoRepository.PermissionState.Unavailable,
    permissionReset: SiteFilteringUiState.ActionProgress =
        SiteFilteringUiState.ActionProgress.IDLE,
    permissionResetConfirmation: Boolean = false,
    siteBlocking: SiteFilteringUiState.ActionProgress =
        SiteFilteringUiState.ActionProgress.IDLE,
    pageZoom: PageZoomState = PageZoomState(),
): BrowserMainUiState {
    val selected = tabs.firstOrNull { it.isSelected }
    val hasBeenNowhere = selected?.hasBeenNowhere ?: true
    val exactRegularSiteSelected = selected != null &&
        !selected.isPrivate &&
        !selected.hasBeenNowhere &&
        selected.host.isNotBlank() &&
        selected.host == navigation.host
    val selectedPermissions = permissions.takeIf { exactRegularSiteSelected }
        ?: SiteInfoRepository.PermissionState.Unavailable
    val regularStart = selected?.let { !it.isPrivate } ?: tabs.none(Tab::isPrivate)
    return BrowserMainUiState(
        host = navigation.host,
        canonicalUrl = navigation.canonicalUrl,
        title = navigation.title,
        // Read off the selected tab rather than added to `NavigationState`, because
        // it is already true of the tab and a second copy of it on the navigation
        // seam would be a second thing every mediator has to remember to set. A
        // list with no selected tab in it answers "not private", which is the
        // closed answer: a surface may not promise the forgetting on a guess.
        isPrivate = selected?.isPrivate == true,
        // Read off the same tab, and the missing answer goes the other way. With no
        // selected tab there is no page, so "has been nowhere" is the closed answer
        // here: it draws TaffyGo's own start content, while the open one would
        // leave the engine's surface uncovered on the strength of a tab nobody
        // could name. Every wrong answer in this direction hides nothing, and every
        // wrong answer in the other one shows a blank document as if it were a page.
        hasBeenNowhere = hasBeenNowhere,
        isLoading = navigation.isLoading,
        isSecure = navigation.isSecure,
        // The engine's in-memory mark first, then the profile store — the
        // same two sources the start page's tiles and the switcher's cards
        // read. A tab whose `TabFavicon` was never constructed still has a
        // face in the pill when the store remembers the host.
        favicon = artwork[selected?.id]?.favicon
            ?: navigation.host.takeIf(String::isNotBlank)?.let { siteMarks[it] },
        failure = navigation.failure,
        // The browser's own facts, carried through: this projection owns
        // which surface shows them, never the numbers themselves.
        filteringActive = navigation.filteringActive,
        blockedRequestCount = navigation.blockedRequestCount,
        siteFilteringOpen = siteFilteringOpen,
        siteFiltering = projectSiteFiltering(
            navigation,
            filtering,
            desktopSite,
            desktopSiteAvailable,
            selectedPermissions,
            permissionReset.takeIf { exactRegularSiteSelected }
                ?: SiteFilteringUiState.ActionProgress.IDLE,
            permissionResetConfirmation &&
                exactRegularSiteSelected &&
                selectedPermissions is SiteInfoRepository.PermissionState.Changed &&
                permissionReset == SiteFilteringUiState.ActionProgress.IDLE,
            siteBlocking,
            pageZoom,
        ),
        findInPage = findInPage,
        savePageOpen = savePageOpen,
        savePage = projectSavePage(
            title = navigation.title,
            host = navigation.host,
            canonicalUrl = navigation.canonicalUrl,
            isPrivate = selected?.isPrivate == true,
            canSave = bookmarksWritable,
            saveStatus = savePageStatus,
        ),
        userTabCount = tabs.count { !it.isTaffyTab },
        taffyTabCount = tabs.count { it.isTaffyTab },
        canGoBack = navigation.canGoBack,
        canGoForward = navigation.canGoForward,
        moreOpen = moreOpen,
        // Carried through rather than decided here. Whether there is something to
        // say is the repository's answer — it is the thing that refused — and this
        // screen's only job is to make sure it gets said.
        notice = notice,
        // A private tab's start page shows nothing of the regular profile
        // (decision 0255): the tiles are that profile's visit counts, so they
        // are left out rather than drawn over a tab that forgets everything.
        // With no selected row the answer fails closed whenever a private tab
        // exists, the rule the address bar's suggestions keep for the same
        // transient list.
        frequent = if (regularStart) {
            frequentTilesFrom(sites, tabs, artwork, siteMarks)
        } else {
            emptyList()
        },
        showsFrequentSites = regularStart,
        // A tab with no page always shows its top bar: hiding it would shrink
        // the start content for no reason. A failure does the same, because
        // the reload control lives in the overflow the top bar carries.
        //
        // Only the top bar. The action row is not in this expression and has
        // no field of its own, because it is never hidden.
        topBarVisible = appearance.topBarVisible ||
            hasBeenNowhere ||
            navigation.failure != null,
        pageBackgroundArgb = if (hasBeenNowhere || navigation.failure != null) {
            null
        } else {
            appearance.backgroundArgb
        },
        startPageGate = startPageGate,
    )
}
