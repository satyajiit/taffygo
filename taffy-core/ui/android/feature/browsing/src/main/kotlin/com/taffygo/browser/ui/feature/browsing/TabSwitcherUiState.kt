// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TabId

/**
 * Screen SCR-104 — the tab switcher.
 *
 * Three lists rather than one with flags on it. Screen catalog row SCR-104 puts
 * Taffy's tabs in their own collapsed group, and the handoff's mock 04 splits
 * yours from private with a segmented control; a screen that had to filter one
 * list every frame would get one of the two wrong eventually.
 *
 * Taffy's group hangs off the user's own tabs only. Taffy never opens a private
 * tab — a tab that forgets everything when it closes is no use to a task that
 * has to cite where a fact came from — so there is no private Taffy group and
 * no field here that could describe one.
 */
data class TabSwitcherUiState(
    /** Which segment is showing. */
    val group: TabSwitcherGroup = TabSwitcherGroup.YOURS,
    /** The tabs the user opened. */
    val yourTabs: List<TabCard> = emptyList(),
    /** The tabs that forget everything when they close. */
    val privateTabs: List<TabCard> = emptyList(),
    /** The tabs Taffy opened for a task. */
    val taffyTabs: List<TabCard> = emptyList(),
    /** Whether Taffy's group is expanded. */
    val taffyGroupExpanded: Boolean = false,
    /** Whether the close-everything confirmation is up. */
    val closeVisibleConfirmationVisible: Boolean = false,
    /** What the search field holds. Empty means the grid is not filtered. */
    val searchQuery: String = "",
    /** Cards chosen for Ask Taffy. Empty means the active tab is the default. */
    val selectedForAsk: Set<TabId> = emptySet(),
    /** Whether a long-press has put the grid in multi-select. */
    val isSelecting: Boolean = false,
) {
    /**
     * Every tab in the segment showing, filter or no filter.
     *
     * The grid draws this, and closing everything starts from it
     * ([closeVisibleTabs]), which is why it is not the filtered list: "close
     * these tabs" has to mean the group, or a search typed and forgotten would
     * quietly change what a destructive button does.
     */
    val visibleTabs: List<TabCard>
        get() = when (group) {
            TabSwitcherGroup.YOURS -> yourTabs
            TabSwitcherGroup.PRIVATE -> privateTabs
        }

    /**
     * What closing every tab closes.
     *
     * The segment showing and, beside the person's own tabs, every one of
     * Taffy's that no task is still holding: one whose task has ended or is
     * gone, and one restored with no creating task at all. The sheet promises
     * that tabs Taffy opened for a running task stay open, and that is the
     * whole promise — keeping every Taffy tab left fourteen of them on a phone
     * long after the errands that opened them, to be closed one at a time
     * (decision 0236). The private segment holds no Taffy tab, so it closes
     * only its own.
     */
    val closeVisibleTabs: List<TabCard>
        get() = when (group) {
            TabSwitcherGroup.YOURS -> yourTabs + taffyTabs.filterNot(TabCard::heldByTask)
            TabSwitcherGroup.PRIVATE -> privateTabs
        }

    /** The cards the grid actually draws once the search field is read. */
    val matchingTabs: List<TabCard>
        get() = if (searchQuery.isBlank()) {
            visibleTabs
        } else {
            val needle = searchQuery.trim()
            visibleTabs.filter {
                it.title.contains(needle, ignoreCase = true) ||
                    it.host.contains(needle, ignoreCase = true)
            }
        }

    /** Whether the segment has tabs but the search hid all of them. */
    val searchFoundNothing: Boolean
        get() = searchQuery.isNotBlank() && visibleTabs.isNotEmpty() && matchingTabs.isEmpty()

    /** Whether there is a group to show at all. */
    val hasTaffyTabs: Boolean
        get() = taffyTabs.isNotEmpty()

    /**
     * Whether Taffy's group belongs on the screen as it is drawn now.
     *
     * The private segment never shows it, because nothing in it could be there.
     */
    val showsTaffyGroup: Boolean
        get() = hasTaffyTabs && group == TabSwitcherGroup.YOURS

    /**
     * Every open tab, which is what the count line means and nothing else.
     *
     * A statement about what is open, on the one screen where all three groups
     * are visible and a person can go and look at each of them. It is
     * deliberately **not** what the workspace action means — see
     * [workspaceTabCount], and the defect that made the difference worth
     * writing down.
     */
    val totalTabCount: Int
        get() = yourTabs.size + privateTabs.size + taffyTabs.size

    /**
     * How many tabs a workspace could actually be built from.
     *
     * "New workspace from 4 tabs…" is a promise about scope, and it was
     * counting tabs that can never be in one. With three ordinary tabs and one
     * private tab open, the button offered four and the consent it led to
     * scoped two — the private tab because decision 0035 forbids it, and the
     * nameless one because decision 0034 does. A control that names a bigger
     * number than the consent screen behind it teaches a person that the
     * private tab is in scope, which is the opposite of what the product
     * promises them.
     *
     * So this counts what the Ask sheet's consent row would offer, by the same
     * three narrowings and in the same direction — Taffy's own tabs are not the person's to offer,
     * a private tab is not a source, and a tab that has been nowhere names no
     * page. [yourTabs] has already applied the first two, which is why only the
     * third is written here.
     *
     * It counts tabs and not origins, because the label says tabs. Two of the
     * person's tabs on one host are two tabs and one page, and the sheet
     * saying "the 1 page you chose" after this says "from 2 tabs" is two true
     * sentences about two different things.
     */
    val workspaceTabIds: List<TabId>
        get() = yourTabs.filter { it.canAskAbout }.map { it.id }

    /** The announced count and the sources carried into Ask are one list. */
    val workspaceTabCount: Int
        get() = workspaceTabIds.size

    /**
     * Whether there is anything a workspace could be made from.
     *
     * Asked of [workspaceTabCount] rather than of the total, so the action is
     * absent exactly where the start rule would refuse — a person whose open
     * tabs are all private is not offered a button that leads to a sheet
     * telling them there is nothing to read (decision 0035's first consequence).
     */
    val canStartWorkspace: Boolean
        get() = workspaceTabCount > 0

    /**
     * Tabs Ask Taffy will open the sheet with.
     *
     * A non-empty selection wins. With nothing chosen, the active tab is the
     * default — if it is one the person may offer.
     */
    val askTaffyTabIds: List<TabId>
        get() {
            val checked = buildList {
                yourTabs.addCheckedIdsTo(this)
                privateTabs.addCheckedIdsTo(this)
                taffyTabs.addCheckedIdsTo(this)
            }
            if (checked.isNotEmpty()) return checked
            return firstCard { it.isSelected && it.canAskAbout }
                ?.let { listOf(it.id) }
                .orEmpty()
        }

    val canAskTaffy: Boolean
        get() = askTaffyTabIds.isNotEmpty()

    private fun List<TabCard>.addCheckedIdsTo(destination: MutableList<TabId>) {
        for (card in this) {
            if (card.checkedForAsk) destination += card.id
        }
    }

    private inline fun firstCard(predicate: (TabCard) -> Boolean): TabCard? =
        yourTabs.firstOrNull(predicate)
            ?: privateTabs.firstOrNull(predicate)
            ?: taffyTabs.firstOrNull(predicate)
}
