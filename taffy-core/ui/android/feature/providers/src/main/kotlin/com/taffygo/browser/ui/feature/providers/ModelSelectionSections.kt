// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyPressable
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.taffyGroupedCardItems
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One provider's models, under that provider's name.
 *
 * The heading answers whichever question the screen has left open. Narrowed to
 * one provider the answer is already in the title, so the block draws no
 * heading of its own and the group names — the kind of work — carry the list.
 * Across the whole catalog the provider is the open question, so it heads the
 * block and the kind of work sits under it.
 */
internal fun LazyListScope.modelProviderBlockItems(
    block: ModelSelectionUiState.Block,
    showProviderName: Boolean,
    onIntent: (ModelSelectionIntent) -> Unit,
) {
    if (showProviderName) {
        item(key = "provider-${block.providerId}", contentType = "provider-heading") {
            TaffySectionHeader(
                title = block.providerName,
                modifier = Modifier.testTag("$MODEL_BLOCK_TEST_TAG_PREFIX${block.providerId}"),
            )
        }
    }
    // What was shown and what there is, when they differ. The list is what
    // survived the budget every provider shares and the count is what the
    // catalog carries; drawing only the list would present part of it as all
    // of it, which is the failure decision 0098 section 4 names.
    if (block.truncated) {
        item(key = "kept-${block.providerId}", contentType = "kept") {
            Text(
                text = taffyString(
                    R.string.taffy_providers_models_showing_of,
                    taffyCount(block.listedCount),
                    taffyCount(block.catalogCount),
                ),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.caution,
                modifier = Modifier.testTag("$MODEL_TRUNCATED_TEST_TAG_PREFIX${block.providerId}"),
            )
        }
    }
    if (block.locked) {
        item(key = "locked-reason-${block.providerId}", contentType = "locked-reason") {
            Text(
                text = taffyString(lockedReason(block)),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(
                    "$MODEL_LOCKED_REASON_TEST_TAG_PREFIX${block.providerId}",
                ),
            )
        }
    }
    block.groups.forEach { group ->
        modelCapabilityGroupItems(block = block, group = group, onIntent = onIntent)
    }
}

/**
 * What a locked provider needs, said as the thing to do.
 *
 * A row that cannot be used is not a fault to report; it is a step nobody has
 * taken yet, except where the product itself has declined the provider — and
 * then the sentence says so rather than inviting a person into a page that
 * would refuse them.
 */
private fun lockedReason(block: ModelSelectionUiState.Block): Int = when (block.offer) {
    ProviderRowOffer.Configure -> R.string.taffy_providers_models_locked_key
    ProviderRowOffer.SignIn -> R.string.taffy_providers_models_locked_plan
    ProviderRowOffer.EditEndpoint -> R.string.taffy_providers_models_locked_endpoint
    is ProviderRowOffer.Blocked -> R.string.taffy_providers_models_locked_blocked
}

/** One kind of work, and the models the catalog listed for it. */
private fun LazyListScope.modelCapabilityGroupItems(
    block: ModelSelectionUiState.Block,
    group: ModelSelectionUiState.Group,
    onIntent: (ModelSelectionIntent) -> Unit,
) {
    val groupKey = "${block.providerId}-${group.capability.name.lowercase()}"
    item(key = "group-heading-$groupKey", contentType = "model-group-heading") {
        Text(
            text = taffyString(capabilityTitle(group.capability)),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.semantics { heading() },
        )
    }
    val firstModelId = group.rows.first().modelId
    taffyGroupedCardItems(
        values = group.rows,
        key = { row -> "model-${row.providerId}-${row.modelId}" },
        contentType = { "model" },
        testTag = { row ->
            if (row.modelId == firstModelId) {
                "$MODEL_GROUP_TEST_TAG_PREFIX${group.capability.name.lowercase()}"
            } else {
                null
            }
        },
    ) { row ->
        ModelSelectionRowItem(
            row = row,
            onIntent = onIntent,
            onOpenProvider = { onIntent(ModelSelectionIntent.OpenProvider(block)) },
        )
    }
}

/** The heading over the providers that still need setting up. */
@Composable
internal fun ModelLockedHeader(
    providerCount: Int,
    expanded: Boolean,
    onToggle: () -> Unit,
) {
    val title = taffyString(R.string.taffy_providers_models_locked_title)
    // The hub's own count of providers, because it is the same count of the
    // same things and two plurals would eventually disagree about the word.
    val counted = taffyPlural(
        R.plurals.taffy_providers_group_count,
        providerCount,
        taffyCount(providerCount),
    )
    TaffyPressable(
        onClick = onToggle,
        role = Role.Button,
        modifier = Modifier.fillMaxWidth(),
        testTag = MODEL_LOCKED_HEADER_TEST_TAG,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                .padding(vertical = TaffyTheme.spacing.step),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TaffySectionHeader(title = title, modifier = Modifier.weight(1f))
            Text(
                text = counted,
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textSecondary,
            )
            Text(
                text = taffyString(
                    if (expanded) {
                        R.string.taffy_providers_models_locked_hide
                    } else {
                        R.string.taffy_providers_models_locked_show
                    },
                ),
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary,
            )
        }
    }
}

/** The tags screen SCR-417's semantics tests name. */
const val MODEL_BLOCK_TEST_TAG_PREFIX: String = "model_block_"
const val MODEL_GROUP_TEST_TAG_PREFIX: String = "model_group_"
const val MODEL_LOCKED_REASON_TEST_TAG_PREFIX: String = "model_locked_reason_"
const val MODEL_LOCKED_HEADER_TEST_TAG: String = "model_locked_header"
const val MODEL_TRUNCATED_TEST_TAG_PREFIX: String = "model_truncated_"
