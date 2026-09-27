// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The start page's box, for a preview and for a semantics test.
 *
 * Both hosts take their box as a slot ([StartPageComposerSlot]) rather than
 * building one, and this is the other half of that: the two stateless halves
 * render under `TaffyPreview` with no window behind them, so there is no
 * back-stack entry to build a view model on and nothing that could hand one
 * out. A slot means each of those callers gets a real box — the same
 * composable the product draws, with the same tags — over a state it holds
 * itself.
 *
 * [state] is what the box shows and [onIntent] is where its keystrokes go, so a
 * test that wants to type holds its own state and runs [reduceAddressBar] over
 * it, and every caller that only needs a box on the screen passes neither.
 *
 * Debug-only, like everything else in this source set; the product's boxes are
 * built by [rememberStartPageComposer].
 */
internal fun previewStartComposer(
    state: AddressBarUiState = AddressBarUiState(),
    onIntent: (AddressBarIntent) -> Unit = {},
): StartPageComposerSlot = StartPageComposerSlot(typing = state.foldsTheWelcomeAway()) {
    StartPageComposer(
        state = state,
        onIntent = onIntent,
        rowTestTag = NEW_TAB_ADDRESS_TEST_TAG,
        placeholder = taffyString(R.string.taffy_new_tab_focus_address_bar),
        menu = {
            StartPageMenu(
                actions = StartPageMenuActions(openLibrary = {}),
                attached = state.attachedStores,
                onToggleStore = { onIntent(AddressBarIntent.ToggleStore(it)) },
                onChooseShape = { onIntent(AddressBarIntent.ChooseShape(it)) },
                onDismiss = { onIntent(AddressBarIntent.DismissMenu) },
            )
        },
    )
    StartPageResults(state = state, onIntent = onIntent)
}
