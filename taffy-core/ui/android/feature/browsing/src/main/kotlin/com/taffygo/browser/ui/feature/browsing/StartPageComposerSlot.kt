// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.Composable

/**
 * The box the start body centres, handed in by whichever screen is drawing that
 * body.
 *
 * One parameter rather than two, because the two halves are never useful apart:
 * [typing] is a fact about the state [content] is drawing, and a body given one
 * without the other would arrange itself for a box it is not showing. Both hosts
 * own an address composer of their own and pass it down through the page area,
 * the way the assistant pill and the form Taffy holds open are already passed —
 * the difference being that this one belongs to the same feature and is built by
 * the screen rather than by the shell.
 *
 * Public because the two stateless halves it is a parameter of are — a preview
 * and a semantics test render them directly — and for no other reason: nothing
 * outside this feature builds one, and [rememberStartPageComposer] is internal
 * so nothing outside it can.
 */
class StartPageComposerSlot(
    /** Whether the person has words in the box, which is what collapses the welcome. */
    val typing: Boolean,
    /** The box itself, and the reading and suggestions it draws beneath it. */
    val content: @Composable () -> Unit,
)

/**
 * Whether the box has taken the page over.
 *
 * **Words, and not a caret.** A person who taps the box has not asked for a
 * different page — they have asked to type on this one, and a page that empties
 * itself the moment it is touched is the full-screen change decision 0131
 * removed, wearing the same layout. So the greeting stays, the tiles stay, and
 * the box stays where decision 0050 put it until there is something to say
 * underneath it.
 *
 * A task under way folds it the same way, for the same reason: the panel that
 * says what Taffy is doing stands where the reading and the suggestions stood,
 * and it needs their room.
 *
 * One function rather than the same expression in two places: the product's box
 * and the preview's box would otherwise be two rules that agree until one of
 * them is edited.
 */
internal fun AddressBarUiState.foldsTheWelcomeAway(): Boolean =
    input.isNotBlank() || starting || started != null
