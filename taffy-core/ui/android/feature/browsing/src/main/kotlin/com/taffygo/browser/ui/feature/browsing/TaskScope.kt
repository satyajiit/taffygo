// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

/**
 * What the offered tabs may contribute to a task's scope, and nothing else.
 *
 * Two decisions are applied here and both narrow. Decision 0034: a tab that
 * has been nowhere has no origin, so it has nothing to put in scope. Decision
 * 0035: a private tab forgets everything when it closes, so it is not a source
 * a task may read or record. Every function here only ever removes entries.
 * None can add one, which is what makes this safe to sit above the grant
 * rather than beside it — a composer's consent row narrows what the person
 * is asked to consent to, and narrowing is the one direction anything
 * downstream of a grant may move in.
 */
internal object TaskScope {

    /** One pass over page hosts, shared by disclosure and start rules. */
    data class Summary(
        val sources: List<String>,
        val hasDuplicateSite: Boolean,
    )

    /**
     * The sources the named tabs offer, or the selected tab when [selectedIds]
     * is omitted.
     *
     * An ask is tied to tabs the person offered. Other open tabs are not
     * implicit consent. Private tabs, Taffy's own tabs, and tabs that have
     * been nowhere are never sources (decisions 0034 and 0035).
     *
     * When [selectedIds] is absent, a projection that reports no selected tab
     * or more than one still returns no source, which is the address-bar path.
     * When ids are present, every matching eligible tab contributes, and
     * duplicate hosts collapse to one origin.
     */
    fun fromTabs(tabs: List<Tab>, selectedIds: List<TabId>? = null): List<String> =
        summarizePages(pagesFromTabs(tabs, selectedIds)).sources

    /**
     * The pages the named tabs offer, one chip per tab.
     *
     * Two tabs on one host stay two pages. Private tabs, Taffy's tabs, and
     * tabs that have been nowhere never become a chip. Named ids whose tab
     * has closed stay as a closed chip until the person removes them.
     */
    fun pagesFromTabs(tabs: List<Tab>, selectedIds: List<TabId>? = null): List<AttachedPage> {
        if (selectedIds == null) {
            val current = currentAskTab(tabs) ?: return emptyList()
            return if (isEligibleAskTab(current)) listOf(current.toAttachedPage()) else emptyList()
        }
        return attachedPagesFromIds(tabs, selectedIds)
    }

    /**
     * Whether a private tab named a page this ask is not offering.
     *
     * The Ask sheet uses this for one thing only: to say something true when
     * it has no sources to name. A private tab that has been nowhere is not
     * counted, because decision 0034 had already removed it and nothing was
     * withheld on privacy grounds; nor is one of Taffy's own, which is not the
     * person's tab to offer.
     *
     * This answer never leaves the sheet. It is not part of the typed start
     * intent, so no record below it learns that a private tab exists.
     */
    fun privateTabsWithheld(tabs: List<Tab>): Boolean =
        tabs.any { it.isPrivate && !it.isTaffyTab && it.host.isNotBlank() }

    /**
     * The rule itself, over the hosts the browser reported.
     *
     * A tab that has been nowhere reports no host. It names no source, it is
     * not counted, and it is not started with the task. A duplicate host is
     * one source, not two, because scope is a set of origins rather than a
     * list of tabs.
     */
    fun fromHosts(hosts: List<String>): List<String> =
        summarizeHosts(hosts).sources

    /** Live attached pages only; a closed chip no longer contributes scope. */
    fun summarizePages(pages: List<AttachedPage>): Summary {
        val seen = LinkedHashSet<String>(pages.size.coerceAtMost(MAX_SCOPE_SET_CAPACITY))
        var duplicate = false
        for (page in pages) {
            val host = page.host
            if (page.closed || host.isBlank()) continue
            if (!seen.add(host)) duplicate = true
        }
        return Summary(sources = seen.toList(), hasDuplicateSite = duplicate)
    }

    /**
     * Deduplicate in insertion order and detect repetition in the same pass.
     * Blank values name no site and therefore cannot create a duplicate site.
     */
    fun summarizeHosts(hosts: List<String>): Summary {
        val seen = LinkedHashSet<String>(hosts.size.coerceAtMost(MAX_SCOPE_SET_CAPACITY))
        var duplicate = false
        for (host in hosts) {
            if (host.isBlank()) continue
            if (!seen.add(host)) duplicate = true
        }
        return Summary(sources = seen.toList(), hasDuplicateSite = duplicate)
    }

    private const val MAX_SCOPE_SET_CAPACITY = 64
}
