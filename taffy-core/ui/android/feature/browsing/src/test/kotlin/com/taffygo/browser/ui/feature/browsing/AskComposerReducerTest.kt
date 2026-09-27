// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.MAX_ERRAND_NEW_SOURCES
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The Ask overlay keeps recognized tasks, treats other readings as questions
 * about its pages, and retains the started task through the next keystroke.
 */
class AskComposerReducerTest {

    @Test
    fun `on the overlay a word the resolver would search for is a question for Taffy`() {
        val state = reduceAddressBar(
            state = AddressBarUiState(conditions = asking),
            intent = AddressBarIntent.InputChanged("tapir"),
            resolve = { AddressBarInterpretation.Search(it) },
            suggest = ::noRows,
        )

        assertEquals(AddressBarInterpretation.AskTaffy("tapir"), state.reading)
        assertTrue(state.interpretation is AddressBarInterpretation.Search)
    }

    @Test
    fun `a question over a page is a summary of that page and over none an errand`() {
        val overPage = AddressBarUiState(conditions = asking, attachedPages = listOf(docs))
            .typed("is this fee refundable?")
        val start = overPage.start as TaskStartDecision.Start
        assertEquals(TaskTemplate.SUMMARIZE_EVIDENCE, start.request.template)
        assertEquals(listOf("docs.example.test"), start.request.consent.sourceHosts)

        val overNone = reduceAddressBar(
            state = overPage,
            intent = AddressBarIntent.RemovePage(docs.tabId),
            resolve = ::asks,
            suggest = ::noRows,
        )
        assertTrue(overNone.attachedPages.isEmpty())
        assertEquals(
            TaskTemplate.WEB_ERRAND,
            (overNone.start as TaskStartDecision.Start).request.template,
        )
    }

    @Test
    fun `a research shape stated on the overlay starts there over its pages`() {
        val state = AddressBarUiState(
            conditions = asking,
            attachedPages = listOf(docs, shop),
            shape = TaskTemplate.COMPARE_PRODUCTS,
        ).typed("which is cheaper?")

        val start = state.start as TaskStartDecision.Start
        assertEquals(TaskTemplate.COMPARE_PRODUCTS, start.request.template)
        assertEquals(listOf("docs.example.test", "shop.example.test"), start.request.consent.sourceHosts)
    }

    @Test
    fun `an inferred comparison keeps its shape and exact attached-page consent`() {
        val inferred = AddressBarInterpretation.TaskForTaffy("compare these phones", TaskTemplate.COMPARE_PRODUCTS)
        val state = AddressBarUiState(conditions = asking, attachedPages = listOf(docs, shop))
            .resolvedAs(inferred)

        assertEquals(inferred, state.reading)
        val request = (state.start as TaskStartDecision.Start).request
        assertEquals(TaskTemplate.COMPARE_PRODUCTS, request.template)
        assertEquals(inferred.input, request.goal)
        assertEquals(listOf("docs.example.test", "shop.example.test"), request.consent.sourceHosts)
        assertEquals(false, request.consent.sourceDiscoveryEnabled)
        assertEquals(0, request.consent.newSourceCap)
        assertEquals(ProviderRoute.DIRECT_WITH_YOUR_KEY, request.consent.providerRoute)
    }

    @Test
    fun `an inferred download over one attached page remains a bounded errand`() {
        val inferred = AddressBarInterpretation.TaskForTaffy("download the sample PDF", TaskTemplate.WEB_ERRAND)
        val state = AddressBarUiState(conditions = asking, attachedPages = listOf(docs))
            .resolvedAs(inferred)

        assertEquals(inferred, state.reading)
        val request = (state.start as TaskStartDecision.Start).request
        assertEquals(TaskTemplate.WEB_ERRAND, request.template)
        assertEquals(inferred.input, request.goal)
        assertEquals(listOf("docs.example.test"), request.consent.sourceHosts)
        assertTrue(request.consent.sourceDiscoveryEnabled)
        assertEquals(MAX_ERRAND_NEW_SOURCES, request.consent.newSourceCap)
        assertEquals(ProviderRoute.DIRECT_WITH_YOUR_KEY, request.consent.providerRoute)
    }

    @Test
    fun `a stated shape still overrides an inferred task until the chip is cleared`() {
        val inferred = AddressBarInterpretation.TaskForTaffy("download the sample PDF", TaskTemplate.WEB_ERRAND)
        val state = AddressBarUiState(
            conditions = asking, attachedPages = listOf(docs), shape = TaskTemplate.SUMMARIZE_EVIDENCE,
        ).resolvedAs(inferred)
        val request = (state.start as TaskStartDecision.Start).request
        assertEquals(TaskTemplate.SUMMARIZE_EVIDENCE, request.template)
        assertEquals(false, request.consent.sourceDiscoveryEnabled)

        val cleared = reduceAddressBar(state, AddressBarIntent.ClearShape, ::asks, ::noRows)
        assertEquals(inferred, cleared.reading)
        assertEquals(TaskTemplate.WEB_ERRAND, (cleared.start as TaskStartDecision.Start).request.template)
    }

    @Test
    fun `locations and search readings on the overlay stay questions about its pages`() {
        val readings = listOf(
            AddressBarInterpretation.GoTo("https://shop.example.test/compare", "shop.example.test"),
            AddressBarInterpretation.Search("cheaper phone prices"),
        )
        readings.forEach { inferred ->
            val state = AddressBarUiState(conditions = asking, attachedPages = listOf(docs))
                .resolvedAs(inferred)
            assertEquals(AddressBarInterpretation.AskTaffy(inferred.input), state.reading)
            val request = (state.start as TaskStartDecision.Start).request
            assertEquals(TaskTemplate.SUMMARIZE_EVIDENCE, request.template)
            assertEquals(listOf("docs.example.test"), request.consent.sourceHosts)
            assertEquals(false, request.consent.sourceDiscoveryEnabled)
        }
    }

    @Test
    fun `an inferred comparison cannot replace missing duplicate or closed sources with discovery`() {
        val inferred = AddressBarInterpretation.TaskForTaffy("compare these phones", TaskTemplate.COMPARE_PRODUCTS)
        val onePage = AddressBarUiState(conditions = asking, attachedPages = listOf(docs)).resolvedAs(inferred)
        listOf(
            onePage,
            onePage.copy(attachedPages = emptyList()),
            onePage.copy(attachedPages = listOf(docs, docs.copy(tabId = TabId("same-host")))),
        ).forEach { invalid ->
            assertEquals(TaskStartDecision.Refused(TaskStartRefusal.WRONG_PAGE_COUNT), invalid.start)
        }
        assertEquals(
            TaskStartDecision.Refused(TaskStartRefusal.PAGE_CLOSED),
            onePage.copy(attachedPages = listOf(docs.copy(closed = true), shop)).start,
        )
    }

    @Test
    fun `an inferred task keeps readiness active-task and source-count refusals`() {
        val inferred = AddressBarInterpretation.TaskForTaffy("download the sample PDF", TaskTemplate.WEB_ERRAND)
        val state = AddressBarUiState(conditions = asking, attachedPages = listOf(docs)).resolvedAs(inferred)
        val conditions = listOf(
            asking.copy(availability = CoreUiAvailability.UNAVAILABLE) to TaskStartRefusal.CORE_NOT_READY,
            asking.copy(readiness = TaffyReadiness.Unknown) to TaskStartRefusal.NOT_READY_YET,
            asking.copy(readiness = TaffyReadiness.NotSetUp) to TaskStartRefusal.SETUP_NEEDED,
            asking.copy(taskAlreadyRunning = true) to TaskStartRefusal.ALREADY_RUNNING,
        )
        conditions.forEach { (given, expected) ->
            assertEquals(TaskStartDecision.Refused(expected), state.copy(conditions = given).start)
        }
        assertEquals(
            TaskStartDecision.Refused(TaskStartRefusal.WRONG_PAGE_COUNT),
            state.copy(attachedPages = listOf(docs, shop)).start,
        )
    }

    @Test
    fun `the same shape stated off the overlay still leaves the box`() {
        val state = AddressBarUiState(conditions = ready, shape = TaskTemplate.COMPARE_PRODUCTS)
            .typed("which is cheaper?")

        assertNull(state.start)
    }

    @Test
    fun `a closed page refuses the start until it is taken off`() {
        val closed = docs.copy(closed = true)
        val state = AddressBarUiState(conditions = asking, attachedPages = listOf(closed, shop))
            .typed("compare these")

        assertEquals(TaskStartDecision.Refused(TaskStartRefusal.PAGE_CLOSED), state.start)

        val removed = reduceAddressBar(
            state = state,
            intent = AddressBarIntent.RemovePage(closed.tabId),
            resolve = ::asks,
            suggest = ::noRows,
        )
        assertTrue(removed.start is TaskStartDecision.Start)
    }

    @Test
    fun `confirming the sheet puts the ticked tabs on the question and keeps a closed one`() {
        val closed = AttachedPage(TabId("gone"), "Old", "old.example.test", closed = true)
        val tabs = listOf(docsTab, shopTab)
        val state = AddressBarUiState(
            conditions = asking,
            attachedPages = listOf(docs, closed),
            pages = askPagesSnapshot(tabs),
            attachOpen = true,
        )

        val confirmed = reduceAddressBar(
            state = state,
            intent = AddressBarIntent.ConfirmAttachPages(listOf(shop.tabId)),
            resolve = ::asks,
            suggest = ::noRows,
        )

        assertEquals(listOf(shop.tabId, closed.tabId), confirmed.attachedPages.map { it.tabId })
        assertTrue(confirmed.attachedPages[1].closed)
        assertEquals(false, confirmed.attachOpen)
    }

    @Test
    fun `opening the sheet closes the plus and dismissing it changes nothing else`() {
        val opened = reduceAddressBar(
            state = AddressBarUiState(conditions = asking, menuOpen = true),
            intent = AddressBarIntent.OpenAttachPages,
            resolve = ::asks,
            suggest = ::noRows,
        )
        assertTrue(opened.attachOpen)
        assertEquals(false, opened.menuOpen)

        val dismissed = reduceAddressBar(opened, AddressBarIntent.DismissAttachPages, ::asks, ::noRows)
        assertEquals(opened.copy(attachOpen = false), dismissed)
    }

    @Test
    fun `typing under an answered task keeps the task and the pages`() {
        val answered = AddressBarUiState(
            conditions = asking,
            attachedPages = listOf(docs),
            started = StartedTask("task-1", "what is this page for?"),
        )

        val typing = reduceAddressBar(
            state = answered,
            intent = AddressBarIntent.InputChanged("and the fee?"),
            resolve = ::asks,
            suggest = ::noRows,
        )

        assertEquals(StartedTask("task-1", "what is this page for?"), typing.started)
        assertEquals(listOf(docs), typing.attachedPages)
        assertEquals("and the fee?", typing.input)
    }

    @Test
    fun `trying again on an emptied overlay box puts the request back and starts it`() {
        val ended = AddressBarUiState(
            conditions = asking,
            started = StartedTask("task-1", "what is this page for?"),
        )

        val again = reduceAddressBar(ended, AddressBarIntent.TryAgain, ::asks, ::noRows)

        assertEquals("what is this page for?", again.input)
        assertTrue(again.starting)
        assertNull(again.started)
    }

    @Test
    fun `the retry row changes nothing on screen by itself`() {
        val notReady = AddressBarUiState(
            conditions = asking.copy(availability = CoreUiAvailability.UNAVAILABLE),
        ).typed("anything")

        assertEquals(
            TaskStartDecision.Refused(TaskStartRefusal.CORE_NOT_READY),
            notReady.start,
        )
        assertEquals(notReady, reduceAddressBar(notReady, AddressBarIntent.RetryCore, ::asks, ::noRows))
    }

    @Test
    fun `leaving the task empties the box but keeps what the tabs offer`() {
        val snapshot = askPagesSnapshot(listOf(docsTab))
        val state = AddressBarUiState(
            conditions = asking,
            pages = snapshot,
            attachedPages = listOf(docs),
            started = StartedTask("task-1", "q"),
        ).typed("next")

        val left = reduceAddressBar(state, AddressBarIntent.LeaveTask, ::asks, ::noRows)

        assertEquals("", left.input)
        assertNull(left.started)
        assertEquals(snapshot, left.pages)
        assertEquals(asking, left.conditions)
    }

    // -----------------------------------------------------------------------

    private val ready = StartConditions(
        readiness = TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY),
        availability = CoreUiAvailability.READY,
    )
    private val asking = ready.copy(asksInPlace = true)

    private val docsTab = Tab(TabId("tab_docs"), "Retention policy", "docs.example.test", isSelected = true)
    private val shopTab = Tab(TabId("tab_shop"), "Product listing", "shop.example.test")
    private val docs = docsTab.toAttachedPage()
    private val shop = shopTab.toAttachedPage()

    private fun AddressBarUiState.typed(input: String): AddressBarUiState =
        reduceAddressBar(this, AddressBarIntent.InputChanged(input), ::asks, ::noRows)

    private fun AddressBarUiState.resolvedAs(reading: AddressBarInterpretation): AddressBarUiState =
        reduceAddressBar(this, AddressBarIntent.InputChanged(reading.input), { reading }, ::noRows)

    private fun asks(input: String): AddressBarInterpretation = AddressBarInterpretation.AskTaffy(input)

    private fun noRows(@Suppress("UNUSED_PARAMETER") input: String): List<Suggestion> = emptyList()
}
