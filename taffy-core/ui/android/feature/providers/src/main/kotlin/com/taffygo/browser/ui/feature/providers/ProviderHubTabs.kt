// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffySegmentedControl
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-404's category tabs: the product's segmented control, with one
 * segment per way in.
 *
 * ## Why this is the shared control and not a bespoke row
 *
 * It was a scrolling row of its own for one rebuild of this screen, and the
 * argument for that was arithmetic: `TaffySegmentedControl` divides its track
 * into equal weighted shares, and **four** shares carrying a glyph, a word and
 * a count all ellipsise together at the largest system font. Removing the
 * Connected tab removed the fourth share and the count with it, and three
 * word-and-glyph segments are exactly the shape this control is for — the same
 * shape the tab switcher, Appearance, Downloads and Clear browsing data draw.
 * A second tab idiom in one product is a thing a person has to learn twice, so
 * the arithmetic having changed, the row goes.
 *
 * ## What moved rather than disappeared
 *
 * The count is no longer drawn as a pill on the tab; it is spoken as part of
 * the segment's one announcement through [TaffySegmentedControl]'s
 * `optionContentDescription`. A screen reader therefore still hears
 * "Subscription. 3 providers." — and a sighted reader who wants the number
 * reads the list itself, which is directly underneath.
 */
@Composable
internal fun ProviderHubTabs(state: ProviderHubUiState, onIntent: (ProviderHubIntent) -> Unit) {
    val groups = ProviderHubGroup.entries
    val counted = groups.map { group ->
        val count = state.countIn(group)
        taffyString(
            R.string.taffy_providers_group_header,
            taffyString(groupTitleRes(group)),
            taffyPlural(R.plurals.taffy_providers_group_count, count, taffyCount(count)),
        )
    }
    TaffySegmentedControl(
        options = groups.map { taffyString(groupTitleRes(it)) },
        selectedIndex = groups.indexOf(state.showing),
        onSelect = { index -> onIntent(ProviderHubIntent.ShowCategory(groups[index])) },
        modifier = Modifier
            .fillMaxWidth()
            .testTag(PROVIDER_HUB_TABS_TEST_TAG),
        optionModifier = { index ->
            Modifier.testTag("$PROVIDER_TAB_TEST_TAG_PREFIX${groups[index].name}")
        },
        optionIcon = { index -> groupGlyph(groups[index]) },
        optionContentDescription = { index -> counted[index] },
    )
}

/**
 * A category with no providers in it, said as a fact about the catalog.
 *
 * A tab that led to a blank page would be indistinguishable from one that had
 * not finished loading, and each of the three is empty for its own reason: the
 * list offers no plan, the list offers no key, or the person has not described
 * a server of their own yet.
 */
@Composable
internal fun NothingInThisCategory(group: ProviderHubGroup) {
    TaffyEmptyState(
        title = taffyString(emptyTitleRes(group)),
        body = taffyString(emptyBodyRes(group)),
        leading = {
            Icon(
                imageVector = groupGlyph(group),
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(EmptyGlyphSize),
            )
        },
        modifier = Modifier.testTag("$PROVIDER_CATEGORY_EMPTY_TEST_TAG_PREFIX${group.name}"),
    )
}

private fun groupTitleRes(group: ProviderHubGroup): Int = when (group) {
    ProviderHubGroup.SUBSCRIPTION -> R.string.taffy_providers_group_subscription
    ProviderHubGroup.BRING_YOUR_OWN_KEY -> R.string.taffy_providers_group_key
    ProviderHubGroup.YOUR_OWN_ENDPOINT -> R.string.taffy_providers_group_endpoint
}

/**
 * The glyph on a tab, which is decoration.
 *
 * The label beside it already names the category, so the glyph carries no
 * content description — one repeated to a screen reader would say everything
 * twice.
 */
private fun groupGlyph(group: ProviderHubGroup): ImageVector = when (group) {
    ProviderHubGroup.SUBSCRIPTION -> TaffyIcon.UserCircle
    ProviderHubGroup.BRING_YOUR_OWN_KEY -> TaffyIcon.Key
    ProviderHubGroup.YOUR_OWN_ENDPOINT -> TaffyIcon.Monitor
}

private fun emptyTitleRes(group: ProviderHubGroup): Int = when (group) {
    ProviderHubGroup.SUBSCRIPTION -> R.string.taffy_providers_group_empty_subscription_title
    ProviderHubGroup.BRING_YOUR_OWN_KEY -> R.string.taffy_providers_group_empty_key_title
    ProviderHubGroup.YOUR_OWN_ENDPOINT -> R.string.taffy_providers_group_empty_endpoint_title
}

private fun emptyBodyRes(group: ProviderHubGroup): Int = when (group) {
    ProviderHubGroup.SUBSCRIPTION -> R.string.taffy_providers_group_empty_subscription_body
    ProviderHubGroup.BRING_YOUR_OWN_KEY -> R.string.taffy_providers_group_empty_key_body
    ProviderHubGroup.YOUR_OWN_ENDPOINT -> R.string.taffy_providers_group_empty_endpoint_body
}

/** The tags screen SCR-404's semantics tests name. */
const val PROVIDER_HUB_TABS_TEST_TAG: String = "providers_hub_tabs"
const val PROVIDER_TAB_TEST_TAG_PREFIX: String = "providers_tab_"
const val PROVIDER_CATEGORY_EMPTY_TEST_TAG_PREFIX: String = "providers_category_empty_"

private val EmptyGlyphSize = 24.dp
