// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate

/** The task a composer starts where it stands, with the consent it showed. */
data class TaskStartRequest(
    val goal: String,
    val template: TaskTemplate,
    val consent: TaskConsentIntent,
)

/**
 * The one start rule, for every composer.
 *
 * It was a rule per screen — the retired preview's own rule, the sheet's
 * `askStartRequest` — and each was a copy of the other with one shape left
 * out. There is no preview screen now: a task starts where it was typed, and
 * the composer's own row shows the consent the preview used to. So the rule
 * lives here, over a shape-neutral input, and every composer calls it: the
 * start page's box, the sheet, the tab switcher. What is true of every start
 * is stated once (a goal, a browser that is answering, one task at a time),
 * and each shape states its own requirement beside its own reason.
 *
 * An errand begins with no page or one exact page and carries the bounded
 * discovery consent the browser enforces (decision 0087); a table is built out
 * of the one page named and may touch no model at all; the two research
 * shapes read every named page before a turn is paid for. Two tabs on one site
 * are one host, as the same-site sentence already tells the person.
 *
 * A store attached whole (decision 0133) rides on any shape a model answers:
 * it reaches Taffy as tools, and tools are only ever called by a model. A
 * table built with no model behind it is handed none, so the consent under
 * the box never says Taffy can search what nothing will.
 */
fun taskStartRequest(
    goal: String,
    template: TaskTemplate,
    sourceHosts: List<String>,
    readiness: TaffyReadiness,
    availability: CoreUiAvailability,
    taskAlreadyRunning: Boolean,
    attachedStores: Set<TaskAttachedStore> = emptySet(),
): TaskStartDecision {
    val trimmed = goal.trim()
    if (trimmed.isEmpty()) return refused(TaskStartRefusal.NO_GOAL)
    if (availability != CoreUiAvailability.READY) return refused(TaskStartRefusal.CORE_NOT_READY)
    if (taskAlreadyRunning) return refused(TaskStartRefusal.ALREADY_RUNNING)
    val hosts = sourceHosts.distinct()
    return when (template) {
        TaskTemplate.WEB_ERRAND -> describeErrand(trimmed, hosts, attachedStores, readiness)
        TaskTemplate.BUILD_A_SOURCE_TABLE ->
            describeTable(trimmed, hosts, attachedStores, readiness)
        TaskTemplate.COMPARE_PRODUCTS ->
            describeResearch(trimmed, template, hosts, attachedStores, readiness, minimumSources = 2)
        TaskTemplate.SUMMARIZE_EVIDENCE ->
            describeResearch(trimmed, template, hosts, attachedStores, readiness, minimumSources = 1)
    }
}

/** An errand may begin with no page or one exact page, then discover more. */
private fun describeErrand(
    goal: String,
    hosts: List<String>,
    stores: Set<TaskAttachedStore>,
    readiness: TaffyReadiness,
): TaskStartDecision {
    if (hosts.size > 1) return refused(TaskStartRefusal.WRONG_PAGE_COUNT)
    val route = modelRoute(readiness) ?: return refused(routeRefusal(readiness))
    return TaskStartDecision.Start(
        TaskStartRequest(
            goal = goal,
            template = TaskTemplate.WEB_ERRAND,
            consent = TaskConsentIntent(
                sourceHosts = hosts,
                sourceDiscoveryEnabled = true,
                newSourceCap = MAX_ERRAND_NEW_SOURCES,
                providerRoute = route,
                attachedStores = stores,
            ),
        ),
    )
}

/**
 * A table is built out of the one page the person named. Discovery stays off
 * and the cap stays at none, because a table's whole claim is that every row
 * came from that page; and it needs no model, so a phone with nothing set up
 * can still build one.
 */
private fun describeTable(
    goal: String,
    hosts: List<String>,
    stores: Set<TaskAttachedStore>,
    readiness: TaffyReadiness,
): TaskStartDecision {
    if (hosts.size != 1) return refused(TaskStartRefusal.WRONG_PAGE_COUNT)
    val route = modelRoute(readiness)
    return TaskStartDecision.Start(
        TaskStartRequest(
            goal = goal,
            template = TaskTemplate.BUILD_A_SOURCE_TABLE,
            consent = TaskConsentIntent(
                sourceHosts = hosts,
                sourceDiscoveryEnabled = false,
                newSourceCap = 0,
                providerRoute = route ?: ProviderRoute.NO_MODEL_REQUIRED,
                attachedStores = if (route == null) emptySet() else stores,
            ),
        ),
    )
}

/** A model-backed research turn reads every exact page before it is paid. */
private fun describeResearch(
    goal: String,
    template: TaskTemplate,
    hosts: List<String>,
    stores: Set<TaskAttachedStore>,
    readiness: TaffyReadiness,
    minimumSources: Int,
): TaskStartDecision {
    if (hosts.size !in minimumSources..MAX_SELECTED_RESEARCH_SOURCES) {
        return refused(TaskStartRefusal.WRONG_PAGE_COUNT)
    }
    val route = modelRoute(readiness) ?: return refused(routeRefusal(readiness))
    return TaskStartDecision.Start(
        TaskStartRequest(
            goal = goal,
            template = template,
            consent = TaskConsentIntent(
                sourceHosts = hosts,
                sourceDiscoveryEnabled = false,
                newSourceCap = 0,
                providerRoute = route,
                attachedStores = stores,
            ),
        ),
    )
}

/**
 * The route a request would take, when something behind it can answer.
 *
 * The comparison reads as a tautology once the rule can only build
 * `Ready(DIRECT_WITH_YOUR_KEY)`, and it is not one: `Ready` is a public data
 * class and anything holding one may name a route that invokes no model.
 * Collapsing this to `ready.route` turns a model-less shape into a paid start.
 */
private fun modelRoute(readiness: TaffyReadiness): ProviderRoute? {
    val ready = readiness as? TaffyReadiness.Ready ?: return null
    return ready.route.takeIf { it == ProviderRoute.DIRECT_WITH_YOUR_KEY }
}

private fun routeRefusal(readiness: TaffyReadiness): TaskStartRefusal =
    if (readiness is TaffyReadiness.Unknown) {
        TaskStartRefusal.NOT_READY_YET
    } else {
        TaskStartRefusal.SETUP_NEEDED
    }

private fun refused(reason: TaskStartRefusal) = TaskStartDecision.Refused(reason)

/** How many exact pages one research turn reads, at most. */
const val MAX_SELECTED_RESEARCH_SOURCES: Int = 8

/** How many sites an errand may open beyond the one it began on, at most. */
const val MAX_ERRAND_NEW_SOURCES: Int = 8

/**
 * An explicitly selected, fully reviewed saved flow reuses the local table's
 * exact-page admission and zero-model consent, with the errand outcome. The
 * caller must submit the opaque offer; the browser and catalogue validate it.
 */
fun savedFlowStartRequest(
    goal: String,
    sourceHost: String,
    availability: CoreUiAvailability,
    taskAlreadyRunning: Boolean,
): TaskStartDecision = when (val decision = taskStartRequest(
    goal, TaskTemplate.BUILD_A_SOURCE_TABLE, listOf(sourceHost),
    TaffyReadiness.NotSetUp, availability, taskAlreadyRunning,
)) {
    is TaskStartDecision.Start -> TaskStartDecision.Start(decision.request.copy(template = TaskTemplate.WEB_ERRAND))
    is TaskStartDecision.Refused -> decision
}
