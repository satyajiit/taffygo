// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import com.taffygo.browser.ui.core.model.PageLoadFailure

/** Rebuilds a destination from a saved route. Called only from [TaffyDestination.fromRoute]. */
internal fun parseTaffyDestination(route: String): TaffyDestination? {
    WITHOUT_ARGUMENTS.firstOrNull { it.route == route }?.let { return it }

    val parts = route.split('/')
    val screenId = parts.firstOrNull() ?: return null
    val arguments = parts.drop(1)
    return when (screenId) {
        TaffyDestination.PageError(PageLoadFailure.OFFLINE).screenId ->
            arguments.singleOrNull()
                ?.let { label -> PageLoadFailure.entries.firstOrNull { it.label == label } }
                ?.let { TaffyDestination.PageError(it) }
        // The identity, and nothing else. A restored ask carries no question
        // and no shape, because neither was ever in the route: both are in the
        // saved state this identity names, and the sheet reads them from
        // there. An ask whose state did not survive therefore opens empty,
        // which is a failure a person can see and correct.
        TaffyDestination.AssistantBar.SCREEN_ID -> arguments.singleOrNull()
            ?.takeIf { it.isNotEmpty() }
            ?.let { askId -> TaffyDestination.AssistantBar(askId = askId) }
        // The identity, and nothing else — the rule the one above states, and the
        // one this destination exists to keep. An errand page whose identity no
        // longer names a live page opens empty and leaves; it never reopens an
        // address a saved route was carrying.
        TaffyDestination.ErrandPage("").screenId -> arguments.singleOrNull()
            ?.takeIf { it.isNotEmpty() }
            ?.let { errandId -> TaffyDestination.ErrandPage(errandId) }
        TaffyDestination.WorkspaceDetail("").screenId ->
            arguments.singleOrNull()?.let { TaffyDestination.WorkspaceDetail(it) }
        TaffyDestination.SourceViewer("", "").screenId ->
            arguments.takeIf { it.size == 2 }
                ?.let { TaffyDestination.SourceViewer(it[0], it[1]) }
        TaffyDestination.FactCorrection("", "").screenId ->
            arguments.takeIf { it.size == 2 }
                ?.let { TaffyDestination.FactCorrection(it[0], it[1]) }
        TaffyDestination.ExportSheet("").screenId ->
            arguments.singleOrNull()?.let { TaffyDestination.ExportSheet(it) }
        TaffyDestination.LibraryCollection("").screenId -> when {
            arguments.size == 1 -> TaffyDestination.LibraryCollection(arguments[0])
            arguments.size == 2 && arguments[1] == "export" ->
                TaffyDestination.LibraryExport(arguments[0])
            else -> null
        }
        TaffyDestination.LibraryItem("", "").screenId ->
            arguments.takeIf { it.size == 2 }
                ?.let { TaffyDestination.LibraryItem(it[0], it[1]) }
        TaffyDestination.SkillDetail("").screenId ->
            arguments.singleOrNull()?.let { TaffyDestination.SkillDetail(it) }
        TaffyDestination.ProviderConfig("").screenId ->
            arguments.singleOrNull()?.let { TaffyDestination.ProviderConfig(it) }
        TaffyDestination.ProviderSignIn("").screenId ->
            arguments.singleOrNull()?.let { TaffyDestination.ProviderSignIn(it) }
        // The two screens whose argument is optional, so the bare identifier is
        // a route rather than a truncation. `SCR-417/` is neither: an empty
        // segment restores as nothing instead of as the whole catalog, because
        // a trailing separator is a damaged route and answering it with a
        // different screen would hide that.
        TaffyDestination.ModelSelection().screenId -> when {
            arguments.isEmpty() -> TaffyDestination.ModelSelection()
            else -> arguments.singleOrNull()
                ?.takeIf { it.isNotEmpty() }
                ?.let { TaffyDestination.ModelSelection(it) }
        }
        TaffyDestination.CustomEndpointSetup().screenId -> when {
            arguments.isEmpty() -> TaffyDestination.CustomEndpointSetup()
            else -> arguments.singleOrNull()
                ?.takeIf { it.isNotEmpty() }
                ?.let { TaffyDestination.CustomEndpointSetup(it) }
        }
        else -> null
    }
}

/**
 * Every destination that takes no arguments.
 *
 * `by lazy`, and see [TaffyDestination.START] for why: built eagerly, this
 * list could hold nulls for any destination whose class initializer was still
 * running when this file first ran. Deferring it to the first `fromRoute` call
 * is enough, because by then every object it names is fully initialized.
 */
private val WITHOUT_ARGUMENTS: List<TaffyDestination> by lazy {
    listOf(
        TaffyDestination.OnboardingWelcome,
        TaffyDestination.MeetTaffy,
        TaffyDestination.LanguageRegion,
        TaffyDestination.GetStarted,
        TaffyDestination.AiSetup,
        TaffyDestination.BrowserProfiles,
        TaffyDestination.Backup,
        TaffyDestination.BrowserMain,
        TaffyDestination.NewTab,
        TaffyDestination.AddressBar,
        TaffyDestination.TabSwitcher,
        TaffyDestination.Downloads,
        TaffyDestination.TaskView,
        TaffyDestination.WorkspaceList,
        TaffyDestination.SettingsHome,
        TaffyDestination.AiAndProviders,
        TaffyDestination.AdAndTrackerBlocking,
        TaffyDestination.Notifications,
        TaffyDestination.Appearance,
        TaffyDestination.History,
        TaffyDestination.Bookmarks,
        TaffyDestination.SiteSettings,
        TaffyDestination.ClearBrowsingData,
        TaffyDestination.General,
        TaffyDestination.Privacy,
        TaffyDestination.TaffySettings,
        TaffyDestination.About,
        TaffyDestination.HelpAndFeedback,
        TaffyDestination.You,
        TaffyDestination.TimeOnSites,
        TaffyDestination.WhatHappened,
        TaffyDestination.SavedSignIns,
        TaffyDestination.SavedDetails,
        TaffyDestination.ConnectedProviders,
        TaffyDestination.LibraryHome,
        TaffyDestination.KeepThis,
        TaffyDestination.Memory,
        TaffyDestination.SkillsList,
        TaffyDestination.Personality,
        TaffyDestination.PersonalityTuning,
    )
}
