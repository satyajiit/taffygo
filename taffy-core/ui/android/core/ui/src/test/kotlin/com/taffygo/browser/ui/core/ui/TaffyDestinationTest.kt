// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import com.taffygo.browser.ui.core.model.PageLoadFailure
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The navigation contract: every destination carries a catalog identifier, a
 * route that survives process death, and arguments a screen can read back.
 */
class TaffyDestinationTest {

    /**
     * Every destination the shell saves is one it can rebuild.
     *
     * The preview in this list carries no goal, and deliberately: its goal is
     * not in its route and cannot come back through one. `a task preview
     * restored from its route is the same preview` is where that is stated.
     */
    @Test
    fun `every destination round trips through its route`() {
        for (destination in everyDestination()) {
            assertEquals(destination, TaffyDestination.fromRoute(destination.route))
        }
    }

    @Test
    fun `a route the application does not have restores as nothing`() {
        assertNull(TaffyDestination.fromRoute("SCR-999"))
        assertNull(TaffyDestination.fromRoute("SCR-305"))
        assertNull(TaffyDestination.fromRoute("SCR-301/"))
        assertNull(TaffyDestination.fromRoute("SCR-301/one_ask/and a question"))
        assertNull(TaffyDestination.fromRoute("SCR-302"))
        assertNull(TaffyDestination.fromRoute("SCR-502"))
        assertNull(TaffyDestination.fromRoute("SCR-503"))
        assertNull(TaffyDestination.fromRoute("SCR-503/only_collection"))
        assertNull(TaffyDestination.fromRoute("SCR-602"))
        assertNull(TaffyDestination.fromRoute("SCR-415"))
        assertNull(TaffyDestination.fromRoute("SCR-416"))
        // An errand page with no identity is not an errand page. It has
        // nowhere to read an address from, so there is nothing it could open.
        assertNull(TaffyDestination.fromRoute("SCR-110"))
        assertNull(TaffyDestination.fromRoute("SCR-110/"))
        assertNull(TaffyDestination.fromRoute("SCR-417/"))
        assertNull(TaffyDestination.fromRoute("SCR-418/"))
        assertNull(TaffyDestination.fromRoute(""))
    }

    /**
     * The whole catalog is a destination, not an argument that went missing.
     *
     * A person can be sent to every model on offer as readily as to one
     * provider's, so the bare screen identifier is a route the shell restores
     * rather than a truncated one it refuses.
     */
    @Test
    fun `model selection restores both of its shapes`() {
        assertEquals("SCR-417", TaffyDestination.ModelSelection().route)
        assertEquals(emptyMap<String, String>(), TaffyDestination.ModelSelection().arguments)
        assertEquals(
            TaffyDestination.ModelSelection(),
            TaffyDestination.fromRoute("SCR-417"),
        )
        assertEquals(
            TaffyDestination.ModelSelection("anthropic"),
            TaffyDestination.fromRoute("SCR-417/anthropic"),
        )
        assertEquals(
            mapOf(TaffyDestination.PROVIDER_ID to "anthropic"),
            TaffyDestination.ModelSelection("anthropic").arguments,
        )
    }

    /** An endpoint being added has no identity yet; an existing one is named. */
    @Test
    fun `custom endpoint setup restores both of its shapes`() {
        assertEquals("SCR-418", TaffyDestination.CustomEndpointSetup().route)
        assertEquals(
            TaffyDestination.CustomEndpointSetup(),
            TaffyDestination.fromRoute("SCR-418"),
        )
        assertEquals(
            TaffyDestination.CustomEndpointSetup("endpoint_one"),
            TaffyDestination.fromRoute("SCR-418/endpoint_one"),
        )
        assertEquals(
            mapOf(TaffyDestination.ENDPOINT_ID to "endpoint_one"),
            TaffyDestination.CustomEndpointSetup("endpoint_one").arguments,
        )
    }

    @Test
    fun `a provider page names only its provider in its route`() {
        assertEquals("SCR-415/anthropic", TaffyDestination.ProviderConfig("anthropic").route)
        assertEquals("SCR-416/anthropic", TaffyDestination.ProviderSignIn("anthropic").route)
        assertEquals(
            mapOf(TaffyDestination.PROVIDER_ID to "anthropic"),
            TaffyDestination.ProviderConfig("anthropic").arguments,
        )
        assertEquals(
            mapOf(TaffyDestination.PROVIDER_ID to "anthropic"),
            TaffyDestination.ProviderSignIn("anthropic").arguments,
        )
    }

    @Test
    fun `a library item names only ids in its route`() {
        val destination = TaffyDestination.LibraryItem("col_one", "item_one")

        assertEquals("SCR-503/col_one/item_one", destination.route)
        assertEquals(
            mapOf(
                TaffyDestination.COLLECTION_ID to "col_one",
                TaffyDestination.ITEM_ID to "item_one",
            ),
            destination.arguments,
        )
        assertEquals(destination, TaffyDestination.fromRoute(destination.route))
    }

    @Test
    fun `a skill detail names only its id in its route`() {
        val destination = TaffyDestination.SkillDetail("skill_one")

        assertEquals("SCR-602/skill_one", destination.route)
        assertEquals(
            mapOf(TaffyDestination.SKILL_ID to "skill_one"),
            destination.arguments,
        )
        assertEquals(destination, TaffyDestination.fromRoute(destination.route))
    }

    @Test
    fun `an ask presents over the screen beneath and other destinations do not`() {
        assertTrue(TaffyDestination.AssistantBar().presentsOverBase)
        assertFalse(TaffyDestination.History.presentsOverBase)
        assertFalse(TaffyDestination.KeepThis.presentsOverBase)
    }

    /**
     * The Assistant bar is opened *about* something too, and it travels the
     * same way a preview's goal does.
     */
    @Test
    fun `an ask carries its question as state and not in its route`() {
        val destination = TaffyDestination.AssistantBar("what is a tapir?")

        assertEquals(
            "${TaffyDestination.AssistantBar.SCREEN_ID}/${destination.askId}",
            destination.route,
        )
        assertFalse(destination.route.contains("tapir"))
        assertEquals(
            mapOf(TaffyDestination.QUESTION to "what is a tapir?"),
            destination.arguments,
        )
    }

    /**
     * Two questions are two screens, which is what stops the second one being
     * silently discarded.
     *
     * An argument is written into a screen's saved state only where that state
     * does not already answer for it, so one shared key would have carried the
     * first question a person ever asked and none of the ones after it.
     */
    @Test
    fun `two asks are different routes, same words or not`() {
        assertEquals(
            3,
            listOf(
                TaffyDestination.AssistantBar("what is a tapir?"),
                TaffyDestination.AssistantBar("what is a tapir?"),
                TaffyDestination.AssistantBar("what is a quokka?"),
            ).map { it.route }.toSet().size,
        )
    }

    /**
     * The bar nobody asked anything has one identity, because it is the
     * collapsed affordance the browser's chrome draws on every frame.
     */
    @Test
    fun `the bar with no question keeps one identity`() {
        val screenId = TaffyDestination.AssistantBar.SCREEN_ID
        val noQuestion = TaffyDestination.AssistantBar.NO_QUESTION

        assertEquals(TaffyDestination.AssistantBar(), TaffyDestination.AssistantBar())
        assertEquals("$screenId/$noQuestion", TaffyDestination.AssistantBar().route)
        assertNull(TaffyDestination.fromRoute(screenId))
    }

    @Test
    fun `every product screen carries a catalog identifier`() {
        val product = everyDestination() - everyProtocolDiagnostic().toSet()

        assertTrue(product.all { it.screenId.matches(Regex("SCR-\\d{3}")) })
    }

    /**
     * The register of protocol diagnostics, and it is empty.
     *
     * A `DIAGNOSTIC-` identifier is the definition of a surface that is not
     * part of the product, and the one this register was written for shipped
     * anyway: the page-snapshot inspector was drawn in the browser's own
     * overflow menu wherever its seam answered, so a person reached a screen
     * that renders document identifiers and the words `true` and `false`.
     * Decision 0129 removed it. Keeping the register asserted empty is what
     * makes the next one a failure here rather than a finding on a phone: a
     * surface reachable by a person is a product surface, and a product
     * surface is designed and carries a catalog identifier.
     */
    @Test
    fun `no destination is a protocol diagnostic`() {
        assertEquals(emptyList<TaffyDestination>(), everyProtocolDiagnostic())
    }

    /** Every destination whose own identifier says it is not a product screen. */
    private fun everyProtocolDiagnostic(): List<TaffyDestination> =
        everyDestination().filter { it.screenId.startsWith("DIAGNOSTIC-") }

    @Test
    fun `a destination with arguments hands them to its screen`() {
        val destination = TaffyDestination.SourceViewer("ws_one", "docs.example.test")

        assertEquals(
            mapOf(
                TaffyDestination.WORKSPACE_ID to "ws_one",
                TaffyDestination.SOURCE_ID to "docs.example.test",
            ),
            destination.arguments,
        )
    }

    @Test
    fun `a destination without arguments hands over nothing`() {
        assertEquals(emptyMap<String, String>(), TaffyDestination.BrowserMain.arguments)
    }

    @Test
    fun `no two destinations share a route`() {
        val routes = everyDestination().map { it.route }

        assertEquals(routes.size, routes.toSet().size)
    }

    private fun everyDestination(): List<TaffyDestination> = listOf(
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
        TaffyDestination.PageError(PageLoadFailure.CERTIFICATE_INVALID),
        TaffyDestination.ErrandPage("errand-1"),
        TaffyDestination.Downloads,
        TaffyDestination.AssistantBar(),
        TaffyDestination.TaskView,
        TaffyDestination.WorkspaceList,
        TaffyDestination.WorkspaceDetail("ws_one"),
        TaffyDestination.SourceViewer("ws_one", "docs.example.test"),
        TaffyDestination.FactCorrection("ws_one", "f1"),
        TaffyDestination.ExportSheet("ws_one"),
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
        TaffyDestination.ProviderConfig("anthropic"),
        TaffyDestination.ProviderSignIn("anthropic"),
        TaffyDestination.ModelSelection(),
        TaffyDestination.ModelSelection("anthropic"),
        TaffyDestination.CustomEndpointSetup(),
        TaffyDestination.CustomEndpointSetup("endpoint_one"),
        TaffyDestination.ConnectedProviders,
        TaffyDestination.LibraryHome,
        TaffyDestination.LibraryCollection("col_one"),
        TaffyDestination.LibraryExport("col_one"),
        TaffyDestination.LibraryItem("col_one", "item_one"),
        TaffyDestination.KeepThis,
        TaffyDestination.Memory,
        TaffyDestination.SkillsList,
        TaffyDestination.SkillDetail("skill_one"),
        TaffyDestination.Personality,
        TaffyDestination.PersonalityTuning,
    )
}
