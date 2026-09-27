// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-103 — the focused address bar.
 *
 * It is still reached from the address pill over a live page, and it is no
 * longer where the start page sends anybody: that screen types into its own box
 * where it stands. What remains here is the same box — [StartPageComposer] and
 * [StartPageResults], the same field and the same reading rows — inside a frame
 * with a way back, so the two surfaces cannot answer "what does this text mean"
 * differently. The plus is absent, because this screen has chrome of its own and
 * its chips below.
 *
 * The field takes focus as the screen arrives, so the keyboard rises with the
 * composer rather than waiting for a second tap on the same box.
 */
@Composable
fun AddressBarScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: AddressBarViewModel = screenViewModel(TaffyDestination.AddressBar)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }
    // A task started from this screen is shown on the page's own pill, not
    // here: this screen is a box the person opens and is finished with, and a
    // started task is the moment it is finished with. Replaced, not pushed, for
    // the reason `AddressBarViewModel.commit` gives.
    val started = state.started
    LaunchedEffect(started) {
        if (started != null) navigator.replaceCurrent(TaffyDestination.BrowserMain)
    }

    AddressBarContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun AddressBarContent(
    state: AddressBarUiState,
    onIntent: (AddressBarIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    // Typing is the only thing to do here, so the field takes focus as the
    // screen arrives and the keyboard rises with it — the person tapped an
    // address box to get here, and a composer that then asks for a second tap
    // on the same box is a door behind a door.
    val focusRequester = remember { FocusRequester() }
    LaunchedEffect(Unit) { focusRequester.requestFocus() }

    TaffyScreen(
        destination = TaffyDestination.AddressBar,
        title = taffyString(R.string.taffy_address_bar_title),
        onBack = { onIntent(AddressBarIntent.Dismiss) },
        modifier = modifier,
    ) {
        StartPageComposer(
            state = state,
            onIntent = onIntent,
            rowTestTag = ADDRESS_ROW_TEST_TAG,
            placeholder = taffyString(R.string.taffy_address_bar_label),
            modifier = Modifier
                .fillMaxWidth()
                .focusRequester(focusRequester),
        )
        StartPageResults(state = state, onIntent = onIntent)
    }
    AddressVoiceInputOverlay(state = state.voiceEntry, onIntent = onIntent)
}

/** One shape's own word, for the plus's menu. */
internal fun templateChipLabel(template: TaskTemplate) = when (template) {
    TaskTemplate.COMPARE_PRODUCTS -> R.string.taffy_address_bar_template_compare_products
    TaskTemplate.SUMMARIZE_EVIDENCE -> R.string.taffy_address_bar_template_summarize_evidence
    TaskTemplate.BUILD_A_SOURCE_TABLE -> R.string.taffy_address_bar_template_build_a_source_table
    TaskTemplate.WEB_ERRAND -> R.string.taffy_address_bar_template_web_errand
}

/** The tags screen SCR-103's semantics tests name. */
const val ADDRESS_INPUT_TEST_TAG: String = "address_bar_input"
const val ADDRESS_ROW_TEST_TAG: String = "address_bar_row"
const val ADDRESS_VOICE_START_TEST_TAG: String = "address_bar_voice_start"
