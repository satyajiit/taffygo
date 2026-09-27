// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-404 — every provider this browser can reach a model through, one
 * category at a time.
 *
 * The screen selects and never edits. A row leads to the surface that owns the
 * change it invites — the provider's own page, the vendor's sign-in, the
 * endpoint the person described — so there is no sheet here that could offer a
 * credential form for a provider that takes none.
 *
 * A provider that is already connected is not on this screen at all — it is on
 * SCR-419, which is where connected providers are managed. So this is the shop
 * and that is the shelf, and neither has to be read past to reach the other.
 * `ProviderHubGroup` says why at length.
 *
 * The three categories are tabs, drawn with the product's own segmented
 * control. They were stacked sections for one rebuild of this screen and that
 * was the wrong shape: the categories are alternatives, so stacking them made
 * every one of them something to scroll past on the way to the others, and the
 * answer to "where do I paste a key" sat under two headings that were not it.
 */
@Composable
fun ProviderHubScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: ProviderHubViewModel = screenViewModel(TaffyDestination.AiAndProviders)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    ProviderHubContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun ProviderHubContent(
    state: ProviderHubUiState,
    onIntent: (ProviderHubIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val visibleRows = state.rowsIn(state.showing)
    TaffyLazyScreen(
        destination = TaffyDestination.AiAndProviders,
        title = taffyString(R.string.taffy_providers_hub_title),
        // What the screen is, said under its name rather than as the first
        // line of the body. The tab row is pinned above the body, and a
        // sentence explaining the screen that appeared *below* the control
        // choosing what the screen shows would be read in the wrong order.
        subtitle = taffyString(R.string.taffy_providers_hub_intro),
        onBack = onBack,
        modifier = modifier,
        // A control that chooses what the body shows cannot scroll away with
        // the body it is choosing, which is what this slot is for. There is
        // nothing to choose between until a list has arrived, so an empty
        // catalog and a roster still coming draw no tabs at all rather than
        // three tabs over nothing.
        header = if (state.sections.isNotEmpty()) {
            { ProviderHubTabs(state = state, onIntent = onIntent) }
        } else {
            null
        },
        // Adding an endpoint is the one thing on this screen that is not
        // already a row, so it sits below the categories rather than inside one
        // of them. It stays available while the catalog is empty or every row
        // is blocked: an address the person supplies is the way out of both.
        footer = { AddYourOwnProviderAction(onIntent = onIntent) },
        listModifier = if (visibleRows.isNotEmpty()) {
            Modifier.testTag(PROVIDER_HUB_LIST_TEST_TAG)
        } else {
            Modifier
        },
    ) {
        when (state.status) {
            ProviderHubStatus.LOADING -> item(contentType = "loading") { ProviderHubSkeleton() }
            ProviderHubStatus.EMPTY -> item(contentType = "empty") { NothingOnOffer() }
            ProviderHubStatus.ALL_CONNECTED ->
                item(contentType = "all-connected") { EverythingAlreadyConnected() }
            ProviderHubStatus.BLOCKED -> {
                item(contentType = "blocked") { EverythingBlocked() }
                providerCategoryItems(state = state, rows = visibleRows, onIntent = onIntent)
            }

            ProviderHubStatus.READY ->
                providerCategoryItems(state = state, rows = visibleRows, onIntent = onIntent)
        }
    }
}

/**
 * The category on show, or why it holds nothing.
 *
 * One category is drawn at a time, so this never draws a heading: the tab above
 * carries the name and the count, and repeating them immediately underneath
 * would say the same thing twice in two type sizes.
 */
private fun LazyListScope.providerCategoryItems(
    state: ProviderHubUiState,
    rows: List<ProviderHubRow>,
    onIntent: (ProviderHubIntent) -> Unit,
) {
    if (rows.isEmpty()) {
        item(key = "empty-${state.showing.name}", contentType = "category-empty") {
            NothingInThisCategory(group = state.showing)
        }
        return
    }
    items(
        items = rows,
        key = ProviderHubRow::providerId,
        contentType = { "provider" },
    ) { row ->
        ProviderHubRowItem(row = row, onIntent = onIntent)
    }
}

/**
 * Every provider in the catalog is already connected, so there is nothing here
 * to add.
 *
 * Said apart from the empty catalog because it is the opposite fact wearing the
 * same blank page: this phone has providers set up on it, and telling somebody
 * the provider list is empty would be a plain untruth. The sentence points at
 * SCR-419, which is where the ones they have are managed.
 */
@Composable
private fun EverythingAlreadyConnected() {
    TaffyEmptyState(
        title = taffyString(R.string.taffy_providers_hub_all_connected_title),
        body = taffyString(R.string.taffy_providers_hub_all_connected_body),
        leading = { StateGlyph(TaffyIcon.CheckCircle) },
        modifier = Modifier.testTag(PROVIDER_HUB_ALL_CONNECTED_TEST_TAG),
    )
}

/**
 * The catalog carries nothing. Said as a fact about the catalog, because
 * nothing is missing from the device and nothing has gone wrong here.
 */
@Composable
private fun NothingOnOffer() {
    TaffyEmptyState(
        title = taffyString(R.string.taffy_providers_hub_empty_title),
        body = taffyString(R.string.taffy_providers_hub_empty_body),
        leading = { StateGlyph(TaffyIcon.Cloud) },
        modifier = Modifier.testTag(PROVIDER_HUB_EMPTY_TEST_TAG),
    )
}

/**
 * Providers arrived and not one of them can be acted on.
 *
 * The rows are still drawn underneath, because each carries its own reason and
 * the summary would otherwise be the only thing a person could read — five
 * providers held shut and five whose sign-in this build lacks are different
 * situations with different answers.
 */
@Composable
private fun EverythingBlocked() {
    TaffyEmptyState(
        title = taffyString(R.string.taffy_providers_hub_blocked_title),
        body = taffyString(R.string.taffy_providers_hub_blocked_body),
        leading = { StateGlyph(TaffyIcon.Warning) },
        modifier = Modifier.testTag(PROVIDER_HUB_BLOCKED_TEST_TAG),
    )
}

/** The mark an empty state stands behind; the well around it is the frame's. */
@Composable
private fun StateGlyph(icon: ImageVector) {
    Icon(
        imageVector = icon,
        contentDescription = null,
        tint = TaffyTheme.colors.textSecondary,
        modifier = Modifier.size(StateGlyphSize),
    )
}

@Composable
private fun AddYourOwnProviderAction(onIntent: (ProviderHubIntent) -> Unit) {
    TaffySecondaryButton(
        label = taffyString(R.string.taffy_providers_add_endpoint),
        onClick = { onIntent(ProviderHubIntent.AddYourOwnProvider) },
        modifier = Modifier.fillMaxWidth(),
        icon = TaffyIcon.Plus,
        testTag = PROVIDER_HUB_ADD_TEST_TAG,
    )
}

/** The tags screen SCR-404's semantics tests name. */
const val PROVIDER_HUB_LIST_TEST_TAG: String = "providers_hub_list"
const val PROVIDER_HUB_EMPTY_TEST_TAG: String = "providers_hub_empty"
const val PROVIDER_HUB_ALL_CONNECTED_TEST_TAG: String = "providers_hub_all_connected"
const val PROVIDER_HUB_BLOCKED_TEST_TAG: String = "providers_hub_blocked"
const val PROVIDER_HUB_ADD_TEST_TAG: String = "providers_hub_add"

private val StateGlyphSize = 24.dp
