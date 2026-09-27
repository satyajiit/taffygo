// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The one start rule, shape by shape: what each needs before it starts, and
 * the reason each gives when it cannot.
 */
class TaskStartRequestTest {

    private val ready = TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY)

    private fun decide(
        template: TaskTemplate,
        hosts: List<String> = emptyList(),
        goal: String = "download my policy document",
        readiness: TaffyReadiness = ready,
        availability: CoreUiAvailability = CoreUiAvailability.READY,
        running: Boolean = false,
        stores: Set<TaskAttachedStore> = emptySet(),
    ) = taskStartRequest(goal, template, hosts, readiness, availability, running, stores)

    private fun started(decision: TaskStartDecision): TaskStartRequest =
        (decision as TaskStartDecision.Start).request

    private fun refused(decision: TaskStartDecision): TaskStartRefusal =
        (decision as TaskStartDecision.Refused).reason

    @Test
    fun `explicit saved flow needs a live core and user start but no model or discovery`() {
        val request = started(savedFlowStartRequest("Run this flow", "portal.example", CoreUiAvailability.READY, false))
        assertEquals(TaskTemplate.WEB_ERRAND, request.template)
        assertEquals(TaskConsentIntent(listOf("portal.example"), false, 0, ProviderRoute.NO_MODEL_REQUIRED), request.consent)
        assertEquals(TaskStartRefusal.NO_GOAL, refused(savedFlowStartRequest(" ", "portal.example", CoreUiAvailability.READY, false)))
        assertEquals(TaskStartRefusal.CORE_NOT_READY, refused(savedFlowStartRequest("Run", "portal.example", CoreUiAvailability.STARTING, false)))
        assertEquals(TaskStartRefusal.ALREADY_RUNNING, refused(savedFlowStartRequest("Run", "portal.example", CoreUiAvailability.READY, true)))
        assertEquals(TaskStartRefusal.SETUP_NEEDED, refused(decide(TaskTemplate.WEB_ERRAND, readiness = TaffyReadiness.NotSetUp)))
    }

    @Test
    fun `an errand starts from the person's words alone and carries the discovery consent`() {
        val request = started(decide(TaskTemplate.WEB_ERRAND))

        assertEquals("download my policy document", request.goal)
        assertEquals(TaskTemplate.WEB_ERRAND, request.template)
        assertEquals(
            TaskConsentIntent(
                sourceHosts = emptyList(),
                sourceDiscoveryEnabled = true,
                newSourceCap = MAX_ERRAND_NEW_SOURCES,
                providerRoute = ProviderRoute.DIRECT_WITH_YOUR_KEY,
            ),
            request.consent,
        )
    }

    @Test
    fun `an attached store rides on every shape a model answers, and not on a table without one`() {
        val stores = setOf(TaskAttachedStore.HISTORY, TaskAttachedStore.OPEN_TABS)
        val page = listOf("docs.example.test")

        assertEquals(stores, started(decide(TaskTemplate.WEB_ERRAND, stores = stores)).consent.attachedStores)
        assertEquals(
            stores,
            started(decide(TaskTemplate.SUMMARIZE_EVIDENCE, page, stores = stores)).consent.attachedStores,
        )
        assertEquals(
            stores,
            started(decide(TaskTemplate.BUILD_A_SOURCE_TABLE, page, stores = stores)).consent.attachedStores,
        )
        // No model behind the route: the table still builds, and a tool
        // nothing will call is not handed over — so the consent cannot say
        // Taffy can search what it will not.
        val unpaid = started(
            decide(TaskTemplate.BUILD_A_SOURCE_TABLE, page, readiness = TaffyReadiness.Unknown, stores = stores),
        )
        assertEquals(ProviderRoute.NO_MODEL_REQUIRED, unpaid.consent.providerRoute)
        assertEquals(emptySet<TaskAttachedStore>(), unpaid.consent.attachedStores)
        // And nothing attached is the ordinary request.
        assertEquals(emptySet<TaskAttachedStore>(), started(decide(TaskTemplate.WEB_ERRAND)).consent.attachedStores)
    }

    @Test
    fun `an errand may begin on one exact page and not two`() {
        val one = started(decide(TaskTemplate.WEB_ERRAND, hosts = listOf("portal.example")))
        assertEquals(listOf("portal.example"), one.consent.sourceHosts)

        val two = decide(TaskTemplate.WEB_ERRAND, hosts = listOf("a.example", "b.example"))
        assertEquals(TaskStartRefusal.WRONG_PAGE_COUNT, refused(two))
    }

    @Test
    fun `what is true of every start is checked first, in the order a person can fix`() {
        assertEquals(TaskStartRefusal.NO_GOAL, refused(decide(TaskTemplate.WEB_ERRAND, goal = "  ")))
        assertEquals(
            TaskStartRefusal.CORE_NOT_READY,
            refused(decide(TaskTemplate.WEB_ERRAND, availability = CoreUiAvailability.STARTING)),
        )
        assertEquals(
            TaskStartRefusal.CORE_NOT_READY,
            refused(decide(TaskTemplate.WEB_ERRAND, availability = CoreUiAvailability.RETRY_REQUIRED)),
        )
        assertEquals(
            TaskStartRefusal.ALREADY_RUNNING,
            refused(decide(TaskTemplate.WEB_ERRAND, running = true)),
        )
    }

    @Test
    fun `a shape that needs a model says what is missing behind the route`() {
        assertEquals(
            TaskStartRefusal.SETUP_NEEDED,
            refused(decide(TaskTemplate.WEB_ERRAND, readiness = TaffyReadiness.NotSetUp)),
        )
        assertEquals(
            TaskStartRefusal.SETUP_NEEDED,
            refused(
                decide(
                    TaskTemplate.WEB_ERRAND,
                    readiness = TaffyReadiness.RouteChosenButNothingBehindIt(ProviderRoute.DIRECT_WITH_YOUR_KEY),
                ),
            ),
        )
        assertEquals(
            TaskStartRefusal.SETUP_NEEDED,
            refused(
                decide(
                    TaskTemplate.WEB_ERRAND,
                    readiness = TaffyReadiness.Ready(ProviderRoute.NO_MODEL_REQUIRED),
                ),
            ),
        )
        assertEquals(
            TaskStartRefusal.NOT_READY_YET,
            refused(decide(TaskTemplate.WEB_ERRAND, readiness = TaffyReadiness.Unknown)),
        )
        assertEquals(
            ProviderRoute.DIRECT_WITH_YOUR_KEY,
            started(
                decide(
                    TaskTemplate.WEB_ERRAND,
                    readiness = TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY),
                ),
            ).consent.providerRoute,
        )
    }

    @Test
    fun `a table is built out of the one page named and needs no model`() {
        val table = started(
            decide(
                TaskTemplate.BUILD_A_SOURCE_TABLE,
                hosts = listOf("docs.example"),
                readiness = TaffyReadiness.NotSetUp,
            ),
        )
        assertEquals(
            TaskConsentIntent(
                sourceHosts = listOf("docs.example"),
                sourceDiscoveryEnabled = false,
                newSourceCap = 0,
                providerRoute = ProviderRoute.NO_MODEL_REQUIRED,
            ),
            table.consent,
        )
        assertEquals(
            ProviderRoute.DIRECT_WITH_YOUR_KEY,
            started(decide(TaskTemplate.BUILD_A_SOURCE_TABLE, hosts = listOf("docs.example")))
                .consent.providerRoute,
        )
        assertEquals(
            TaskStartRefusal.WRONG_PAGE_COUNT,
            refused(decide(TaskTemplate.BUILD_A_SOURCE_TABLE)),
        )
        assertEquals(
            TaskStartRefusal.WRONG_PAGE_COUNT,
            refused(decide(TaskTemplate.BUILD_A_SOURCE_TABLE, hosts = listOf("a.example", "b.example"))),
        )
    }

    @Test
    fun `comparison needs two named pages and a summary needs one, up to a turn's worth`() {
        assertEquals(
            TaskStartRefusal.WRONG_PAGE_COUNT,
            refused(decide(TaskTemplate.COMPARE_PRODUCTS, hosts = listOf("a.example"))),
        )
        val compared = started(
            decide(TaskTemplate.COMPARE_PRODUCTS, hosts = listOf("a.example", "b.example")),
        )
        assertEquals(listOf("a.example", "b.example"), compared.consent.sourceHosts)
        assertEquals(false, compared.consent.sourceDiscoveryEnabled)
        assertEquals(0, compared.consent.newSourceCap)

        assertEquals(
            TaskStartRefusal.WRONG_PAGE_COUNT,
            refused(decide(TaskTemplate.SUMMARIZE_EVIDENCE)),
        )
        val summarized = started(decide(TaskTemplate.SUMMARIZE_EVIDENCE, hosts = listOf("a.example")))
        assertEquals(TaskTemplate.SUMMARIZE_EVIDENCE, summarized.template)
        val tooMany = List(MAX_SELECTED_RESEARCH_SOURCES + 1) { "site$it.example" }
        assertEquals(
            TaskStartRefusal.WRONG_PAGE_COUNT,
            refused(decide(TaskTemplate.SUMMARIZE_EVIDENCE, hosts = tooMany)),
        )
    }

    @Test
    fun `two tabs on one site are one host, and the goal is the words without their edges`() {
        val request = started(
            decide(
                TaskTemplate.SUMMARIZE_EVIDENCE,
                hosts = listOf("docs.example", "docs.example"),
                goal = "  is this fee refundable?  ",
            ),
        )
        assertEquals(listOf("docs.example"), request.consent.sourceHosts)
        assertEquals("is this fee refundable?", request.goal)
    }
}
