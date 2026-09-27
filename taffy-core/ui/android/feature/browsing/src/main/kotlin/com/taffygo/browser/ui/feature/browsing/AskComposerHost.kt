// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBackHandler
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySetupNeededPanel
import com.taffygo.browser.ui.core.ui.rememberScreenViewModelStoreOwner
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The Ask overlay's box: the start page's composer, standing over a page,
 * with the pages the question is about as chips under it and Taffy's answer
 * above it once there is one (decision 0135).
 *
 * It is the start page's [AddressBarViewModel] on the overlay's own entry,
 * keyed apart from the box the start page draws so the two drafts cannot
 * reach each other. The view model reads which host it is on from the
 * question the destination handed it, and everything that differs on this
 * host — every reading starts here, the pages decide the shape, the box
 * stays for the next question — is a rule in the state and the reducer, so
 * a host test can hold it. This function only draws.
 *
 * ## What stands in the card
 *
 * Nothing set up to answer, and no task yet: the set-up panel, in place of
 * the box, with the same two doors the sheet offered. Otherwise the
 * conversation so far, when a task has been started from here — the
 * person's words and Taffy's answer under them, then what Taffy is doing —
 * filled by the shell from the assistant feature through [conversation],
 * because browsing does not depend on the assistant; and beneath it the
 * box, for the first question or the next one.
 *
 * ## Back
 *
 * Back with words in the box clears the words and stays: a person who
 * typed and changed their mind has not asked to lose the page they were
 * asking about. Back with an empty box reaches the overlay's own handler,
 * which closes it. The two handlers are registered in that order on purpose
 * — this one inside the frame, so it wins first.
 */
@Composable
internal fun AskComposer(
    host: TaffyDestination.AssistantBar,
    navigator: TaffyNavigator,
    conversation: @Composable (
        started: StartedTask,
        onTryAgain: () -> Unit,
        onLeave: () -> Unit,
    ) -> Unit,
    modifier: Modifier = Modifier,
) {
    val composerKey = "${host.route}/$ASK_COMPOSER_KEY"
    val viewModel: AddressBarViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(host, key = composerKey),
        key = composerKey,
    )
    val state by viewModel.state.collectAsStateWithLifecycle()
    val onIntent: (AddressBarIntent) -> Unit = { viewModel.onIntent(it, navigator) }

    val focusManager = LocalFocusManager.current
    val keyboard = LocalSoftwareKeyboardController.current
    TaffyBackHandler(enabled = state.input.isNotBlank()) {
        keyboard?.hide()
        focusManager.clearFocus()
        onIntent(AddressBarIntent.InputChanged(NOTHING_ASKED))
    }

    val started = state.started
    val readiness = state.conditions.readiness
    AskComposerLayout(
        modifier = modifier,
        composer = {
            StartPageComposer(
                state = state,
                onIntent = onIntent,
                rowTestTag = ASK_ADDRESS_TEST_TAG,
                placeholder = taffyString(askPlaceholder(state.attachedPages.size)),
                showOptions = false,
                modifier = Modifier.padding(top = TaffyTheme.spacing.tight),
                menu = {
                    StartPageMenu(
                        actions = StartPageMenuActions(
                            openLibrary = { navigator.goTo(TaffyDestination.LibraryHome) },
                        ),
                        attached = state.attachedStores,
                        onToggleStore = { onIntent(AddressBarIntent.ToggleStore(it)) },
                        onChooseShape = { onIntent(AddressBarIntent.ChooseShape(it)) },
                        onDismiss = { onIntent(AddressBarIntent.DismissMenu) },
                        onAddPages = { onIntent(AddressBarIntent.OpenAttachPages) },
                    )
                },
            )
        },
    ) {
        if (readiness.needsSetup && started == null && state.input.isBlank() && !state.savedFlows.visible) {
            TaffySetupNeededPanel(
                title = taffyString(askSetupTitle(readiness)),
                body = taffyString(askSetupBody(readiness)),
                primaryLabel = taffyString(R.string.taffy_ask_setup_primary),
                onPrimary = { navigator.goTo(TaffyDestination.AiAndProviders) },
                secondaryLabel = taffyString(R.string.taffy_ask_setup_secondary),
                onSecondary = { navigator.goBack() },
                testTag = ASK_SETUP_TEST_TAG,
            )
        }
        if (started != null) {
            conversation(
                started,
                { onIntent(AddressBarIntent.TryAgain) },
                {
                    onIntent(AddressBarIntent.LeaveTask)
                    navigator.goBack()
                },
            )
        }
        StartPageOptionChips(state = state, onIntent = onIntent)
        StartPageResults(state = state, onIntent = onIntent)
    }
    AddressVoiceInputOverlay(state = state.voiceEntry, onIntent = onIntent)
    if (state.attachOpen) {
        AttachPagesSheet(
            destination = host,
            alreadyAttached = state.attachedPages.map { it.tabId },
            onConfirm = { onIntent(AddressBarIntent.ConfirmAttachPages(it)) },
            onDismiss = { onIntent(AddressBarIntent.DismissAttachPages) },
        )
    }
}

/** Keep the question and send action visible while the conversation scrolls. */
@Composable
internal fun AskComposerLayout(
    modifier: Modifier = Modifier,
    composer: @Composable () -> Unit,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Column(
            modifier = Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            content = content,
        )
        composer()
    }
}

/** The empty box's hint, by how many pages the question is about. */
private fun askPlaceholder(pages: Int): Int = when (pages) {
    0 -> R.string.taffy_ask_field_hint_none
    1 -> R.string.taffy_ask_field_hint_page
    else -> R.string.taffy_ask_field_hint_pages
}

/** What the overlay's box is filed under, beside the overlay's own view model. */
private const val ASK_COMPOSER_KEY = "ask"

/** A box with its words taken back out of it. */
private const val NOTHING_ASKED = ""

/** The tags the semantics tests name. */
const val ASK_ADDRESS_TEST_TAG: String = "ask_address"
const val ASK_SETUP_TEST_TAG: String = "ask_setup_needed"
