// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * Destinations that share a list-detail pair on a tablet.
 *
 * The shell uses these groups to keep a list visible while its detail is
 * open. A feature never names another feature's screen; it names a
 * destination, and the group is a property of the destination.
 */
object TaffyDestinationGroups {

    /** Settings home and the sections that open from it. */
    fun isSettings(destination: TaffyDestination): Boolean = when (destination) {
        TaffyDestination.SettingsHome,
        TaffyDestination.AiAndProviders,
        TaffyDestination.AdAndTrackerBlocking,
        TaffyDestination.Notifications,
        TaffyDestination.Appearance,
        TaffyDestination.TaffySettings,
        TaffyDestination.Privacy,
        TaffyDestination.General,
        TaffyDestination.BrowserProfiles,
        TaffyDestination.Backup,
        TaffyDestination.About,
        TaffyDestination.HelpAndFeedback,
        TaffyDestination.SiteSettings,
        TaffyDestination.ClearBrowsingData,
        TaffyDestination.SkillsList,
        is TaffyDestination.SkillDetail,
        TaffyDestination.Personality,
        TaffyDestination.PersonalityTuning,
        is TaffyDestination.ProviderConfig,
        is TaffyDestination.ProviderSignIn,
        is TaffyDestination.ModelSelection,
        is TaffyDestination.CustomEndpointSetup,
        TaffyDestination.ConnectedProviders,
        -> true
        else -> false
    }

    /** You and every screen that opens from it. */
    fun isYou(destination: TaffyDestination): Boolean = when (destination) {
        TaffyDestination.You,
        TaffyDestination.TimeOnSites,
        TaffyDestination.WhatHappened,
        TaffyDestination.SavedSignIns,
        TaffyDestination.SavedDetails,
        TaffyDestination.Memory,
        -> true
        else -> false
    }

    /** Library home and every screen that opens from it. */
    fun isLibrary(destination: TaffyDestination): Boolean = when (destination) {
        TaffyDestination.LibraryHome,
        is TaffyDestination.LibraryCollection,
        is TaffyDestination.LibraryExport,
        is TaffyDestination.LibraryItem,
        TaffyDestination.KeepThis,
        -> true
        else -> false
    }

    /** The workspace list and every screen that opens from one workspace. */
    fun isWorkspace(destination: TaffyDestination): Boolean = when (destination) {
        TaffyDestination.WorkspaceList,
        is TaffyDestination.WorkspaceDetail,
        is TaffyDestination.SourceViewer,
        is TaffyDestination.FactCorrection,
        is TaffyDestination.ExportSheet,
        -> true
        else -> false
    }

    /** The workspace the current destination is about, if it is about one. */
    fun workspaceId(destination: TaffyDestination): String? = when (destination) {
        is TaffyDestination.WorkspaceDetail -> destination.workspaceId
        is TaffyDestination.SourceViewer -> destination.workspaceId
        is TaffyDestination.FactCorrection -> destination.workspaceId
        is TaffyDestination.ExportSheet -> destination.workspaceId
        else -> null
    }
}
