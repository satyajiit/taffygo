// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.model.TaffyPartPurpose
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyListRow
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The separately labelled product parts half of SCR-203.
 *
 * A status list, not a download manager. Every required part arrives on its
 * own, on any live connection, and is asked for again by the profile when
 * the browser has tried and stopped — so no row here offers a download or a
 * retry, and the only control is Delete, on a part the product would not
 * fetch straight back.
 */
@Composable
internal fun TaffyPartsList(
    state: DownloadsUiState,
    onIntent: (DownloadsIntent) -> Unit,
) {
    if (!state.partsSupported) {
        TaffyEmptyState(
            title = taffyString(R.string.taffy_parts_unsupported_title),
            body = taffyString(R.string.taffy_parts_unsupported_body),
        )
        return
    }
    if (state.hasNoParts) {
        TaffyEmptyState(
            title = taffyString(R.string.taffy_parts_empty_title),
            body = taffyString(R.string.taffy_parts_empty_body),
        )
        return
    }
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        state.parts.forEach { part -> PartRow(part = part, onIntent = onIntent) }
    }
}

@Composable
private fun PartRow(part: TaffyPart, onIntent: (DownloadsIntent) -> Unit) {
    val name = taffyString(purposeName(part.purpose))
    val status = taffyString(statusName(part))
    val progress = part.fraction
        ?.let { taffyString(R.string.taffy_downloads_progress, (it * 100).toInt()) }
        ?: taffyString(R.string.taffy_downloads_progress_unknown)
    TaffyListRow(
        title = name,
        supporting = taffyString(R.string.taffy_parts_supporting, status, progress),
        accessibleDescription = taffyString(R.string.taffy_parts_description, name, status, progress),
        testTag = "$PART_TEST_TAG_PREFIX${part.id.value}",
        trailing = {
            if (part.availability == TaffyPartAvailability.READY && !part.purpose.required) {
                TaffyDangerButton(
                    label = taffyString(R.string.taffy_parts_remove),
                    onClick = { onIntent(DownloadsIntent.RemovePart(part.id)) },
                    testTag = "$REMOVE_PART_TEST_TAG_PREFIX${part.id.value}",
                )
            }
        },
    )
}

internal fun statusName(part: TaffyPart): Int {
    part.hold?.let { return holdName(it) }
    return when (part.availability) {
        TaffyPartAvailability.READY -> R.string.taffy_parts_state_ready
        TaffyPartAvailability.CHECKING -> R.string.taffy_parts_state_checking
        TaffyPartAvailability.PARTIAL -> R.string.taffy_parts_state_downloading
        // A required part that is missing is on its way; an optional one that is
        // missing is simply not needed yet, and "waiting" would promise a fetch
        // nothing has asked for.
        TaffyPartAvailability.MISSING -> if (part.purpose.required) {
            R.string.taffy_parts_state_missing
        } else {
            R.string.taffy_parts_state_not_needed
        }
    }
}

private fun purposeName(purpose: TaffyPartPurpose): Int = when (purpose) {
    TaffyPartPurpose.PYTHON_LIBRARY -> R.string.taffy_parts_purpose_python_library
    TaffyPartPurpose.PYTHON_PACKAGES -> R.string.taffy_parts_purpose_python_packages
    TaffyPartPurpose.MODEL -> R.string.taffy_parts_purpose_model
    TaffyPartPurpose.MODEL_TOKENIZER -> R.string.taffy_parts_purpose_model_tokenizer
    TaffyPartPurpose.BLOCK_LIST -> R.string.taffy_parts_purpose_block_list
    TaffyPartPurpose.COUNTRY_FLAGS -> R.string.taffy_parts_purpose_country_flags
    TaffyPartPurpose.START_SCENES -> R.string.taffy_parts_purpose_start_scenes
}

private fun holdName(hold: TaffyPartHold): Int = when (hold) {
    TaffyPartHold.NOT_AVAILABLE_FOR_THIS_DEVICE -> R.string.taffy_parts_hold_not_available
    TaffyPartHold.NOT_PUBLISHED -> R.string.taffy_parts_hold_not_published
    TaffyPartHold.ATTEMPTS_SPENT -> R.string.taffy_parts_hold_attempts_spent
    TaffyPartHold.WRONG_CONTENTS -> R.string.taffy_parts_hold_wrong_contents
    TaffyPartHold.CONNECTION_NOT_ALLOWED -> R.string.taffy_parts_hold_connection
    TaffyPartHold.TURNED_OFF -> R.string.taffy_parts_hold_turned_off
    TaffyPartHold.PRODUCT_DEFECT -> R.string.taffy_parts_hold_product_defect
}

const val PART_TEST_TAG_PREFIX: String = "taffy_part_"
const val REMOVE_PART_TEST_TAG_PREFIX: String = "taffy_part_remove_"
