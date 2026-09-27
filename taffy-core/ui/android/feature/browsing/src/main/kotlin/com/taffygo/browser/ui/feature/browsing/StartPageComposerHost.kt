// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBackHandler
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.rememberScreenViewModelStoreOwner
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The start page's box, built once for whichever screen is drawing it.
 *
 * Both hosts call this and neither builds a box of its own, which is what keeps
 * screen SCR-102 and an empty tab on SCR-101 from disagreeing about the same
 * moment — the property [StartPageBody] already has for everything else on that
 * surface.
 *
 * ## The composer belongs to the host's own entry
 *
 * [host] is the destination the caller *is*, never the address bar: SCR-101
 * passes `BrowserMain` and SCR-102 passes `NewTab`. A screen that reached for
 * another destination's entry would take out a back-stack entry nothing ever
 * pushed, and the store releases an entry when its route is popped — so that
 * entry would live for the window's life, and the first real visit to the
 * destination it borrowed would destroy the view model this screen is still
 * drawing from.
 *
 * The key is the host's route with `/composer` after it, the way
 * `BrowserMainScreen` already keys its takeover view model: a view model store
 * files by key, so two view models on one entry sharing a key would be one
 * entry keeping whichever class asked last. Two entries and two keys mean two
 * saved-state handles, so this box's draft, the other host's, and screen
 * SCR-103's cannot reach each other.
 *
 * ## Leaving empties it
 *
 * SCR-101 is the bottom of the back stack and never leaves it, so nothing ever
 * releases its entry and a draft would otherwise wait in the next empty tab's
 * box. [showsStartBody] going false is this surface being left, and the box
 * empties itself then — see [AddressBarIntent.Left], which navigates nowhere
 * precisely because the screen has already gone.
 *
 * Back is the other way out, and it goes through the same intent: the box
 * closes, and the screen it is standing on does not move. That press has to be
 * taken here rather than left to the host, because the host's answer on an
 * empty tab is to put the browser in the background.
 *
 * ## A task started here is drawn here
 *
 * Once the core has admitted a start and named the task, the box gives its
 * place to [taskPanel]: what Taffy is doing, under the person's own words. The
 * panel belongs to the assistant feature and reaches this box as a slot, the
 * way the pill reaches the browsing surface — browsing does not depend on the
 * assistant, and the shell is where they meet. The box comes back when the
 * surface is left, through the same [AddressBarIntent.Left] that empties it,
 * and when the panel hands it back once the task has ended: the same request
 * again ([AddressBarIntent.TryAgain]) keeps the words and starts, and home
 * ([AddressBarIntent.LeaveTask]) empties the box and brings the start page
 * back whole.
 *
 * @param onFirstFocus what the host does the first time somebody puts the caret
 *   in this box. SCR-102 opens the tab the words will commit into and SCR-101
 *   already has one, and it is raised once per box rather than once per focus:
 *   a re-focus after the microphone dialog closes is not a second request for a
 *   tab.
 * @param taskPanel what stands where the box stood while the task it started
 *   is under way, given the two things it may ask of the box once that task
 *   has ended — the same request again, or home; nothing, on a host that has
 *   no panel to offer.
 */
@Composable
internal fun rememberStartPageComposer(
    host: TaffyDestination,
    navigator: TaffyNavigator,
    menuActions: StartPageMenuActions,
    showsStartBody: Boolean,
    onFirstFocus: () -> Unit = {},
    taskPanel: @Composable (
        started: StartedTask,
        onTryAgain: () -> Unit,
        onLeave: () -> Unit,
    ) -> Unit = { _, _, _ -> },
): StartPageComposerSlot {
    val viewModel: AddressBarViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(host),
        key = "${host.route}/$COMPOSER_KEY",
    )
    val state by viewModel.state.collectAsStateWithLifecycle()
    val onIntent: (AddressBarIntent) -> Unit = { viewModel.onIntent(it, navigator) }

    var focused by remember { mutableStateOf(false) }
    var asked by remember { mutableStateOf(false) }
    LaunchedEffect(showsStartBody) {
        if (!showsStartBody) {
            focused = false
            asked = false
            onIntent(AddressBarIntent.Left)
        }
    }

    // Back closes the box before it does anything else, and this is the whole
    // of the affordance the full-screen bar used to provide for free: that
    // screen had a frame with a way out of it, and a field standing in the
    // middle of the page has none. Without this, back on screen SCR-101 reaches
    // that screen's own handler, finds an empty tab with no page history, and
    // puts the browser in the background — a person who tapped the box and
    // changed their mind loses the app.
    //
    // Enabled by focus **or** by words, because the two come apart: the
    // keyboard's own back dismisses the keyboard and leaves the words, and
    // those words are still a box that has been opened and not finished with.
    val focusManager = LocalFocusManager.current
    val keyboard = LocalSoftwareKeyboardController.current
    TaffyBackHandler(enabled = focused || state.input.isNotBlank()) {
        keyboard?.hide()
        focusManager.clearFocus()
        onIntent(AddressBarIntent.Left)
    }

    val started = state.started
    if (started != null) {
        return StartPageComposerSlot(typing = true) {
            taskPanel(
                started,
                { onIntent(AddressBarIntent.TryAgain) },
                { onIntent(AddressBarIntent.LeaveTask) },
            )
        }
    }
    return StartPageComposerSlot(typing = state.foldsTheWelcomeAway()) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            StartPageComposer(
                state = state,
                onIntent = onIntent,
                rowTestTag = NEW_TAB_ADDRESS_TEST_TAG,
                placeholder = taffyString(R.string.taffy_new_tab_focus_address_bar),
                onFocusChanged = { has ->
                    focused = has
                    if (has && !asked) {
                        asked = true
                        onFirstFocus()
                    }
                },
                menu = {
                    StartPageMenu(
                        actions = menuActions,
                        attached = state.attachedStores,
                        onToggleStore = { onIntent(AddressBarIntent.ToggleStore(it)) },
                        onChooseShape = { onIntent(AddressBarIntent.ChooseShape(it)) },
                        onDismiss = { onIntent(AddressBarIntent.DismissMenu) },
                    )
                },
            )
            StartPageResults(state = state, onIntent = onIntent)
        }
        // The microphone's own surface, and the only one it has. A Speak control
        // with no overlay behind it is a microphone that opens invisibly.
        AddressVoiceInputOverlay(state = state.voiceEntry, onIntent = onIntent)
    }
}

/** What the box is filed under, beside its host's own view model. */
private const val COMPOSER_KEY = "composer"
