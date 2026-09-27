// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * Where the plus on the start page's box can still send a person.
 *
 * **This menu is about the question being asked, not about the browser.** It
 * holds what a request can be made out of and the shapes a job can take, and
 * nothing else: You and Settings were here and are gone, because they are
 * somewhere to go rather than something a request can be made out of. The
 * gear in the dock one line below is the way to Settings, and You is a row on
 * that screen — so nothing became unreachable, and decision 0050 section 5's
 * "one door per destination" is whole again.
 *
 * Downloads and Workspaces are absent for the same reason they always were:
 * each owns a slot in the dock, and putting them back behind a menu is the
 * reverse of the change that record documents.
 *
 * **History and Bookmarks no longer navigate from here.** They are attached to
 * the request instead, beside Open tabs, and reach Taffy as tools it may search
 * (decision 0133, which closes OD-136) — so they are intents on the box,
 * [AddressBarIntent.ToggleStore], rather than lambdas here. Library is the one
 * place a person's own pages are kept that still opens: its records already
 * reach Taffy as sources through the Library tools, and attaching it would say
 * the same thing twice.
 *
 * A named lambda rather than one `(TaffyDestination) -> Unit`, because the two
 * screens that draw this box already have intents of their own for this
 * destination and a generic opener would be a second way to reach it. A named
 * lambda rather than one screen's intent type, because the two screens do not
 * share one — see [BrowserMenuTile].
 */
internal class StartPageMenuActions(
    val openLibrary: () -> Unit,
)
