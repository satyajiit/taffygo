// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.ThinkingLevel
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/**
 * Debug-only Compose previews; never part of the product APK.
 *
 * The one this file exists for is the cut list: a provider whose catalog
 * carries more models than the shared budget let through, saying so above
 * the survivors rather than drawing them as though they were everything
 * (decision 0098 section 4).
 */
@ThemePreviews
@Composable
private fun ModelSelectionTruncatedPreview() {
    TaffyPreview(darkTheme = false) {
        ModelSelectionContent(
            state = ModelSelectionUiState(
                status = ModelSelectionUiState.Status.READY,
                providerId = null,
                ready = listOf(truncatedBlock(), wholeBlock()),
            ),
            onIntent = {},
        )
    }
}

/** The budget cut this provider's list, and the page says by how much. */
@ThemePreviews
@Composable
private fun ModelSelectionEmptiedPreview() {
    TaffyPreview(darkTheme = true) {
        ModelSelectionContent(
            state = ModelSelectionUiState(
                status = ModelSelectionUiState.Status.READY,
                providerId = "example-emptied",
                ready = listOf(
                    truncatedBlock().copy(
                        providerId = "example-emptied",
                        groups = emptyList(),
                        listedCount = 0,
                    ),
                ),
            ),
            onIntent = {},
        )
    }
}

private fun truncatedBlock(): ModelSelectionUiState.Block = ModelSelectionUiState.Block(
    providerId = "example-wide",
    providerName = "Example Wide",
    locked = false,
    offer = ProviderRowOffer.Configure,
    groups = listOf(
        ModelSelectionUiState.Group(
            capability = ModelCapability.REASONING,
            rows = listOf(
                modelRow("example-wide", "Example Wide", "wide-large", selected = true),
                modelRow("example-wide", "Example Wide", "wide-small"),
            ),
        ),
    ),
    listedCount = 2,
    catalogCount = 34,
)

private fun wholeBlock(): ModelSelectionUiState.Block = ModelSelectionUiState.Block(
    providerId = "example-plain",
    providerName = "Example Plain",
    locked = false,
    offer = ProviderRowOffer.Configure,
    groups = listOf(
        ModelSelectionUiState.Group(
            capability = ModelCapability.STANDARD,
            rows = listOf(modelRow("example-plain", "Example Plain", "plain-one")),
        ),
    ),
    listedCount = 1,
    catalogCount = 1,
)

private fun modelRow(
    providerId: String,
    providerName: String,
    modelId: String,
    selected: Boolean = false,
): ModelSelectionUiState.Row = ModelSelectionUiState.Row(
    providerId = providerId,
    providerName = providerName,
    modelId = modelId,
    displayName = modelId.replace('-', ' ').replaceFirstChar { it.uppercase() },
    capability = if (providerId == "example-plain") {
        ModelCapability.STANDARD
    } else {
        ModelCapability.REASONING
    },
    reasoning = providerId != "example-plain",
    readsPictures = false,
    contextWindow = 200_000uL,
    selected = selected,
    asking = false,
    locked = false,
    rungs = if (selected) listOf(ThinkingLevel.OFF, ThinkingLevel.LOW, ThinkingLevel.HIGH) else emptyList(),
    standing = if (selected) ThinkingLevel.LOW else null,
)
