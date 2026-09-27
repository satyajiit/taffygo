// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TabId
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * What one card of screen SCR-104 says, and what it refuses to say.
 *
 * The words below stand in for the resources, which is the whole point of
 * splitting the decision from the wording: this suite drives every shape a
 * card can be — with a title, without one, with neither — on a laptop, with no
 * `Context`, no device, and no browser.
 *
 * The rule it exists to hold is a voice rule, not a cosmetic one. A tab that
 * has not been anywhere is on an internal address, and
 * `docs/voice-and-naming.md` does not allow a mechanism to be read out to a
 * person as if it were a name.
 */
class TabCardDescriptionTest {

    private val blankLabel = "New tab"
    private val showingNow = "showing now"
    private val byTaffy = "opened by Taffy"

    /** The English join, which is what the resource says. */
    private fun join(left: String, right: String) = "$left, $right"

    private fun card(
        title: String,
        host: String,
        isSelected: Boolean = false,
        openedByTaffy: Boolean = false,
    ) = TabCard(
        id = TabId("tab_1"),
        title = title,
        host = host,
        isSelected = isSelected,
        openedByTaffy = openedByTaffy,
    )

    private fun describe(
        card: TabCard,
        badge: String? = null,
        chosenForAsk: String? = null,
    ) = tabCardDescription(
        card = card,
        label = tabCardLabel(card, blankLabel),
        showingNow = showingNow,
        openedByTaffy = byTaffy,
        badge = badge,
        join = ::join,
        chosenForAsk = chosenForAsk,
    )

    // -----------------------------------------------------------------------
    // The name on the card.
    // -----------------------------------------------------------------------

    @Test
    fun `a card names the site it is on`() {
        assertEquals(
            "docs.example.test",
            tabCardLabel(card("Retention policy", "docs.example.test"), blankLabel),
        )
    }

    @Test
    fun `a page with no host falls back to what it calls itself`() {
        // A `data:` or `file:` page has no host. It is still somewhere, and the
        // title is the only name it has.
        assertEquals("Local notes", tabCardLabel(card("Local notes", ""), blankLabel))
    }

    @Test
    fun `a tab that has been nowhere is never a blank card`() {
        // The defect this replaces: title and host both empty, so the card drew
        // an empty line and read as nothing at all.
        assertEquals(blankLabel, tabCardLabel(card("", ""), blankLabel))
    }

    // -----------------------------------------------------------------------
    // What a screen reader hears.
    // -----------------------------------------------------------------------

    @Test
    fun `an ordinary card says its title and then its host`() {
        assertEquals(
            "Retention policy, docs.example.test",
            describe(card("Retention policy", "docs.example.test")),
        )
    }

    @Test
    fun `the tab being looked at says so last`() {
        assertEquals(
            "Retention policy, docs.example.test, showing now",
            describe(card("Retention policy", "docs.example.test", isSelected = true)),
        )
    }

    @Test
    fun `a tab that has been nowhere says its name and nothing else`() {
        assertEquals(blankLabel, describe(card("", "")))
    }

    @Test
    fun `a tab that has been nowhere and is showing says only those two things`() {
        // The defect: "about:blank, , showing now" — an internal address, and an
        // empty part between two commas.
        assertEquals("New tab, showing now", describe(card("", "", isSelected = true)))
    }

    @Test
    fun `no part of a description is ever empty`() {
        val descriptions = listOf(
            describe(card("", "")),
            describe(card("", "", isSelected = true)),
            describe(card("", "docs.example.test")),
            describe(card("Retention policy", "")),
            describe(card("", "", openedByTaffy = true), badge = "3 facts"),
        )
        descriptions.forEach { description ->
            assertEquals(
                description,
                description.split(", ").filter { it.isNotBlank() }.joinToString(", "),
            )
        }
    }

    @Test
    fun `a page titled after its own host is not said twice`() {
        assertEquals(
            "docs.example.test",
            describe(card("docs.example.test", "docs.example.test")),
        )
    }

    @Test
    fun `a title with no host is said once rather than as a title and a name`() {
        assertEquals("Local notes", describe(card("Local notes", "")))
    }

    // -----------------------------------------------------------------------
    // Taffy's own tabs.
    // -----------------------------------------------------------------------

    @Test
    fun `a card Taffy opened says so, and says what it took`() {
        assertEquals(
            "Independent review, reviews.example.test, opened by Taffy, 3 facts",
            describe(
                card("Independent review", "reviews.example.test", openedByTaffy = true),
                badge = "3 facts",
            ),
        )
    }

    @Test
    fun `a card Taffy opened that has taken nothing claims nothing`() {
        assertEquals(
            "Independent review, reviews.example.test, opened by Taffy",
            describe(card("Independent review", "reviews.example.test", openedByTaffy = true)),
        )
    }

    @Test
    fun `a badge belongs to Taffy's tabs and to no others`() {
        // A card the user opened has no fact count to report, so a badge handed
        // in for one is not spoken: the amber vocabulary is Taffy's alone.
        assertEquals(
            "Retention policy, docs.example.test",
            describe(card("Retention policy", "docs.example.test"), badge = "3 facts"),
        )
    }

    @Test
    fun `a card chosen for Ask Taffy says so`() {
        assertEquals(
            "Product listing, shop.example.test, chosen for Ask Taffy",
            describe(
                card("Product listing", "shop.example.test").copy(checkedForAsk = true),
                chosenForAsk = "chosen for Ask Taffy",
            ),
        )
    }
}
