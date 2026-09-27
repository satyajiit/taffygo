// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/*
 * Everything one card of screen SCR-104 says about itself, built in one place.
 *
 * The grid used to assemble a card's spoken description at the call site, out
 * of format strings that each assumed their slots were filled: one for the
 * title and the host, another for the close control's name. A tab that had not
 * been anywhere filled none of them, so the browser announced
 * "about:blank, , showing now" and offered a control called "Close ". Both are
 * the same defect — a sentence built from parts nobody checked were there.
 *
 * So the parts are checked here, once, for every card. A part that is empty is
 * not spoken, and a card that knows nothing about itself still has a name.
 */

/**
 * The one name a card shows and speaks.
 *
 * The host, because that is the identity screen SCR-104's mock puts under the
 * preview and the thing a person recognises a tab by. Failing that the page's
 * own title, which is all a `data:` or `file:` page can offer. Failing both —
 * a tab that has not been anywhere — [blankLabel], which is the screen's words
 * for exactly that and never the address behind it.
 */
internal fun tabCardLabel(card: TabCard, blankLabel: String): String =
    card.host.ifBlank { card.title }.ifBlank { blankLabel }

/**
 * What a screen reader is told about one card.
 *
 * Every argument except [card] is already in the reader's language: this
 * function decides *which* parts are true of this card and in what order, and
 * [join] puts two of them together the way the locale joins a list. Keeping the
 * decision here and the wording in resources is what lets a plain unit test
 * assert that nothing empty is ever spoken, without a device or a `Context`.
 *
 * @param card the card being described.
 * @param label its name, from [tabCardLabel].
 * @param showingNow the words for the tab the person is looking at.
 * @param openedByTaffy the words for a tab Taffy opened for a task.
 * @param badge what Taffy has taken from that page, or null when it has taken
 *   nothing — a Taffy tab with nothing to report says nothing rather than "0".
 * @param chosenForAsk the words for a card in the Ask Taffy selection, or null.
 * @param join joins two parts, in the locale's own punctuation.
 */
internal fun tabCardDescription(
    card: TabCard,
    label: String,
    showingNow: String,
    openedByTaffy: String,
    badge: String?,
    join: (String, String) -> String,
    chosenForAsk: String? = null,
): String {
    val parts = buildList {
        addAll(listOf(card.title, label).filter { it.isNotBlank() }.distinct())
        if (card.isSelected) add(showingNow)
        if (card.openedByTaffy) {
            add(openedByTaffy)
            if (badge != null) add(badge)
        }
        if (card.checkedForAsk && chosenForAsk != null) add(chosenForAsk)
    }
    // [tabCardLabel] never answers blank, so there is always at least one part;
    // the empty string is what a card with no name at all would say, and it is
    // here so that this function cannot be the thing that throws.
    return parts.filter { it.isNotBlank() }.reduceOrNull(join).orEmpty()
}
