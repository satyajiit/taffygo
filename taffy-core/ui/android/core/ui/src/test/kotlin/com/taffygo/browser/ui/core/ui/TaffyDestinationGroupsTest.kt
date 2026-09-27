// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The list-detail groups a tablet host uses. A destination that is not in a
 * group stays a single pane at every width.
 */
class TaffyDestinationGroupsTest {

    @Test
    fun `every settings section is in the settings group`() {
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.SettingsHome))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.Appearance))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.AiAndProviders))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.AdAndTrackerBlocking))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.Notifications))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.TaffySettings))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.Privacy))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.General))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.BrowserProfiles))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.Backup))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.About))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.HelpAndFeedback))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.SiteSettings))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.ClearBrowsingData))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.SkillsList))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.SkillDetail("skill_one")))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.Personality))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.PersonalityTuning))
    }

    /**
     * A provider page opens from AI and providers, so the tablet keeps the
     * settings list beside it rather than replacing the window with it.
     */
    @Test
    fun `every provider screen is in the settings group`() {
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.ProviderConfig("anthropic")))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.ProviderSignIn("anthropic")))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.ModelSelection()))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.ModelSelection("anthropic")))
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.CustomEndpointSetup()))
        assertTrue(
            TaffyDestinationGroups.isSettings(TaffyDestination.CustomEndpointSetup("one")),
        )
        assertTrue(TaffyDestinationGroups.isSettings(TaffyDestination.ConnectedProviders))
    }

    @Test
    fun `you and its children are in the you group`() {
        assertTrue(TaffyDestinationGroups.isYou(TaffyDestination.You))
        assertTrue(TaffyDestinationGroups.isYou(TaffyDestination.TimeOnSites))
        assertTrue(TaffyDestinationGroups.isYou(TaffyDestination.WhatHappened))
        assertTrue(TaffyDestinationGroups.isYou(TaffyDestination.SavedSignIns))
        assertTrue(TaffyDestinationGroups.isYou(TaffyDestination.SavedDetails))
        assertTrue(TaffyDestinationGroups.isYou(TaffyDestination.Memory))
        assertFalse(TaffyDestinationGroups.isSettings(TaffyDestination.You))
        assertFalse(TaffyDestinationGroups.isSettings(TaffyDestination.Memory))
        assertFalse(TaffyDestinationGroups.isYou(TaffyDestination.GetStarted))
    }

    @Test
    fun `library and its children are in the library group`() {
        val collection = TaffyDestination.LibraryCollection("col_one")
        val item = TaffyDestination.LibraryItem("col_one", "item_one")

        assertTrue(TaffyDestinationGroups.isLibrary(TaffyDestination.LibraryHome))
        assertTrue(TaffyDestinationGroups.isLibrary(collection))
        assertTrue(TaffyDestinationGroups.isLibrary(item))
        assertTrue(TaffyDestinationGroups.isLibrary(TaffyDestination.KeepThis))
        assertFalse(TaffyDestinationGroups.isYou(TaffyDestination.LibraryHome))
        assertFalse(TaffyDestinationGroups.isSettings(TaffyDestination.KeepThis))
        assertFalse(TaffyDestinationGroups.isLibrary(TaffyDestination.Memory))
    }

    @Test
    fun `a workspace's own screens stay in the workspace group`() {
        val detail = TaffyDestination.WorkspaceDetail("ws_one")
        val source = TaffyDestination.SourceViewer("ws_one", "src_one")

        assertTrue(TaffyDestinationGroups.isWorkspace(TaffyDestination.WorkspaceList))
        assertTrue(TaffyDestinationGroups.isWorkspace(detail))
        assertTrue(TaffyDestinationGroups.isWorkspace(source))
        assertEquals("ws_one", TaffyDestinationGroups.workspaceId(detail))
        assertEquals("ws_one", TaffyDestinationGroups.workspaceId(source))
    }

    @Test
    fun `history and bookmarks stay single pane`() {
        assertFalse(TaffyDestinationGroups.isSettings(TaffyDestination.History))
        assertFalse(TaffyDestinationGroups.isYou(TaffyDestination.History))
        assertFalse(TaffyDestinationGroups.isLibrary(TaffyDestination.History))
        assertFalse(TaffyDestinationGroups.isWorkspace(TaffyDestination.History))
        assertFalse(TaffyDestinationGroups.isSettings(TaffyDestination.Bookmarks))
        assertFalse(TaffyDestinationGroups.isYou(TaffyDestination.Bookmarks))
        assertFalse(TaffyDestinationGroups.isLibrary(TaffyDestination.Bookmarks))
        assertFalse(TaffyDestinationGroups.isWorkspace(TaffyDestination.Bookmarks))
    }

    @Test
    fun `the browser is in no group`() {
        assertFalse(TaffyDestinationGroups.isSettings(TaffyDestination.BrowserMain))
        assertFalse(TaffyDestinationGroups.isYou(TaffyDestination.BrowserMain))
        assertFalse(TaffyDestinationGroups.isLibrary(TaffyDestination.BrowserMain))
        assertFalse(TaffyDestinationGroups.isWorkspace(TaffyDestination.BrowserMain))
        assertNull(TaffyDestinationGroups.workspaceId(TaffyDestination.BrowserMain))
    }
}
