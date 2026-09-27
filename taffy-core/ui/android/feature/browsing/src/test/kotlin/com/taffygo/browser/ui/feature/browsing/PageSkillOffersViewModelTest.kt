// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.api.toSavedFlowReview
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.page.PageIntelligenceRepository
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartFailure
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.PageInspectorAvailability
import taffy.core_api.PageInspectorDocumentKind
import taffy.core_api.PageInspectorDocumentState
import taffy.core_api.PageInspectorDocumentView
import taffy.core_api.PageInspectorDocumentsView
import taffy.core_api.PageInspectorRedactionView
import taffy.core_api.PageInspectorSnapshotResult
import taffy.core_api.PageInspectorSnapshotView
import taffy.core_api.PageInspectorTruncationView
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.SiteSkillOfferAvailability
import taffy.core_api.SiteSkillOfferView
import taffy.core_api.SiteSkillProvenanceView

@OptIn(ExperimentalCoroutinesApi::class)
class PageSkillOffersViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val tab = Tab(TabId("selected"), "Document", "identity.example.test", isSelected = true)
    private val browser = StartTestBrowser(listOf(tab)) { AddressBarInterpretation.GoTo(it, it) }
    private val pages = Pages()
    private val status = MutableStateFlow(pageFlowStatus())
    private val tasks = RecordingTaskRepository()
    private val navigation = RecordingNavigation()

    @Before fun setUp() {
        Dispatchers.setMain(dispatcher)
        browser.navigation.value = NavigationState(host = tab.host, title = tab.title, canonicalUrl = FLOW_TEST_ADDRESS)
    }
    @After fun tearDown() = Dispatchers.resetMain()

    @Test fun `inspection is explicit and learned offer starts its exact opaque identity`() = runTest(dispatcher) {
        val model = model(FixedReadiness(TaffyReadiness.NotSetUp))
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        assertEquals(0, pages.inspections)
        model.open("Use saved flow")
        runCurrent()
        val offer = model.state.value.offers.single()
        val request = (offer.start as TaskStartDecision.Start).request
        assertEquals(TaskTemplate.WEB_ERRAND, request.template)
        assertFalse(request.consent.sourceDiscoveryEnabled)
        assertEquals(0, request.consent.newSourceCap)
        assertEquals(ProviderRoute.NO_MODEL_REQUIRED, request.consent.providerRoute)
        assertTrue(request.consent.attachedStores.isEmpty())
        model.start(offer, navigation)
        model.start(offer, navigation)
        runCurrent()
        assertEquals(listOf("opaque-current-document"), tasks.skillOfferIds)
        assertEquals(1, tasks.starts.size)
        assertTrue(navigation.visited.isEmpty())
        assertTrue(model.state.value.starting)
        tasks.publish(startedTask("new-task", "Use saved flow"))
        runCurrent()
        assertEquals("new-task", tasks.followed)
        assertEquals(listOf(TaffyDestination.TaskView), navigation.visited)
        assertFalse(model.state.value.open)
        assertEquals(1, pages.inspections)
    }

    @Test fun `authored skill keeps source table without discovery`() = runTest(dispatcher) {
        status.value = pageFlowStatus(listOf(pageFlowSkill().copy(
            provenance = SiteSkillProvenanceView.AUTHORED, recorded_from_task_id = null, reviewed_steps = emptyList(),
        )))
        val model = model()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        model.open("Use saved flow")
        runCurrent()
        val request = (model.state.value.offers.single().start as TaskStartDecision.Start).request
        assertEquals(TaskTemplate.BUILD_A_SOURCE_TABLE, request.template)
        assertFalse(request.consent.sourceDiscoveryEnabled)
        assertEquals(0, request.consent.newSourceCap)
    }

    @Test fun `old page offer cannot start after tab or accepted version changes`() = runTest(dispatcher) {
        val model = model()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        model.open("Use saved flow")
        runCurrent()
        val offer = model.state.value.offers.single()
        status.value = pageFlowStatus(listOf(pageFlowSkill().copy(active_version = 2u)))
        model.start(offer, navigation)
        runCurrent()
        assertTrue(tasks.starts.isEmpty())
        assertEquals(PageSkillOffersUiState.Availability.STALE, model.state.value.availability)
        status.value = pageFlowStatus()
        model.open("Use saved flow")
        runCurrent()
        browser.tabsBecome(listOf(tab.copy(id = TabId("other"))))
        model.start(model.state.value.offers.single(), navigation)
        runCurrent()
        assertTrue(tasks.starts.isEmpty())
        assertTrue(model.state.value.offers.isEmpty())
    }

    @Test fun `private page and incomplete learned review never offer a start`() = runTest(dispatcher) {
        val model = model()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        browser.tabsBecome(listOf(tab.copy(isPrivate = true)))
        model.open("Use saved flow")
        runCurrent()
        assertEquals(0, pages.inspections)
        assertEquals(PageSkillOffersUiState.Availability.UNAVAILABLE, model.state.value.availability)
        browser.tabsBecome(listOf(tab))
        status.value = pageFlowStatus(listOf(pageFlowSkill().copy(reviewed_steps = emptyList())))
        model.open("Use saved flow")
        runCurrent()
        assertTrue(model.state.value.offers.isEmpty())
    }

    @Test fun `refused start keeps failure visible and does not navigate`() = runTest(dispatcher) {
        val model = model()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        model.open("Use saved flow")
        runCurrent()
        tasks.startResult = TaffyResult.Failure(FailureReason.STALE_REVISION)
        model.start(model.state.value.offers.single(), navigation)
        runCurrent()
        assertEquals(TaskStartFailure.STALE_REVISION, model.state.value.failure)
        assertFalse(model.state.value.starting)
        assertTrue(navigation.visited.isEmpty())
    }

    @Test fun `navigation during inspection cannot publish the previous pages offer`() = runTest(dispatcher) {
        val pending = CompletableDeferred<Unit>()
        pages.snapshotWait = pending
        val model = model()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        model.open("Use saved flow")
        runCurrent()
        assertEquals(PageSkillOffersUiState.Availability.LOADING, model.state.value.availability)
        browser.navigation.value = browser.navigation.value.copy(canonicalUrl = "https://other.example.test/")
        runCurrent()
        pending.complete(Unit)
        runCurrent()
        assertEquals(PageSkillOffersUiState.Availability.STALE, model.state.value.availability)
        assertTrue(model.state.value.offers.isEmpty())
        assertTrue(tasks.starts.isEmpty())
    }

    @Test fun `closing an inspection cannot reopen the sheet with a late answer`() = runTest(dispatcher) {
        val pending = CompletableDeferred<Unit>()
        pages.snapshotWait = pending
        val model = model()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        model.open("Use saved flow")
        runCurrent()
        model.close()
        pending.complete(Unit)
        runCurrent()
        assertFalse(model.state.value.open)
        assertTrue(model.state.value.offers.isEmpty())
    }

    @Test fun `an unavailable full review does not hide another complete accepted flow`() = runTest(dispatcher) {
        status.value = pageFlowStatus(listOf(pageFlowSkill(), pageFlowSkill().copy(
            skill_id = "missing-review", reviewed_steps = emptyList(),
        )))
        pages.offers = pages.offers + SiteSkillOfferView("opaque-unreviewable", "missing-review", 1u, 1u)
        val model = model()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        model.open("Use saved flow")
        runCurrent()
        assertEquals(PageSkillOffersUiState.Availability.REVIEW_UNAVAILABLE, model.state.value.availability)
        assertEquals("opaque-current-document", model.state.value.offers.single().id)
        model.start(model.state.value.offers.single(), navigation)
        runCurrent()
        assertEquals(listOf("opaque-current-document"), tasks.skillOfferIds)
    }

    @Test fun `opening reviewed page waits for that tab to settle and only offers fresh execution`() = runTest(dispatcher) {
        val requests = SavedFlowPageRequests()
        val model = PageSkillOffersViewModel(browser, pages, status, tasks, FixedReadiness(), flowPages = requests)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        browser.navigation.value = browser.navigation.value.copy(isLoading = true)
        requests.show("Repeat document request", requireNotNull(pageFlowSkill().toSavedFlowReview()), tab.id)
        runCurrent()
        assertEquals(0, pages.inspections)
        assertFalse(model.state.value.open)
        browser.navigation.value = browser.navigation.value.copy(isLoading = false)
        runCurrent()
        assertEquals(1, pages.inspections)
        assertEquals(PageSkillOffersUiState.Availability.READY, model.state.value.availability)
        assertTrue(tasks.starts.isEmpty())
        assertEquals("opaque-current-document", model.state.value.offers.single().id)
        assertEquals("Repeat document request", (model.state.value.offers.single().start as TaskStartDecision.Start).request.goal)
    }

    @Test fun `switching away during reviewed navigation withdraws the pending page offer`() = runTest(dispatcher) {
        val requests = SavedFlowPageRequests()
        val model = PageSkillOffersViewModel(browser, pages, status, tasks, FixedReadiness(), flowPages = requests)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        browser.navigation.value = browser.navigation.value.copy(isLoading = true)
        requests.show("Repeat document request", requireNotNull(pageFlowSkill().toSavedFlowReview()), tab.id)
        runCurrent()
        browser.tabsBecome(listOf(tab.copy(id = TabId("other"))))
        runCurrent()
        browser.navigation.value = browser.navigation.value.copy(isLoading = false)
        runCurrent()
        assertEquals(0, pages.inspections)
        assertFalse(model.state.value.open)
        assertTrue(tasks.starts.isEmpty())
    }

    private fun model(readiness: FixedReadiness = FixedReadiness()) =
        PageSkillOffersViewModel(browser, pages, status, tasks, readiness)

    private class Pages : PageIntelligenceRepository {
        override val isAvailable = true
        var inspections = 0
        var snapshotWait: CompletableDeferred<Unit>? = null
        var offers = listOf(SiteSkillOfferView("opaque-current-document", "download-document", 1u, 1u))
        override suspend fun documents() = PageInspectorDocumentsView(
            PageInspectorAvailability.AVAILABLE,
            listOf(PageInspectorDocumentView("document", PageInspectorDocumentKind.SEMANTIC_PAGE, "identity.example.test", emptyList())),
        )
        override suspend fun snapshot(documentId: String): PageInspectorSnapshotResult {
            inspections++
            snapshotWait?.await()
            return PageInspectorSnapshotResult(PageInspectorAvailability.AVAILABLE, PageInspectorSnapshotView(
                document_id = documentId, document_revision = 1u, host = "identity.example.test",
                secure_context = true, private_profile = false, document_state = PageInspectorDocumentState.ACTIVE,
                adapters = emptyList(), nodes = emptyList(), edges = emptyList(), frames = emptyList(),
                truncation = PageInspectorTruncationView(false, emptyList(), 0u, 0u, 0u, false),
                redaction = PageInspectorRedactionView(0u, 0u, 0u, 0u), warnings = emptyList(),
                site_skill_offer_availability = SiteSkillOfferAvailability.AVAILABLE,
                site_skill_offers = offers,
            ))
        }
        override suspend fun exportSnapshot(requestId: String, documentId: String, format: PageSnapshotExportFormat): Nothing =
            error("Starting a saved flow must not export a page or capture an image")
        override suspend fun cancelExport(requestId: String): Nothing = error("No export started")
    }
}
