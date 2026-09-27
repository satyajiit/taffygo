// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.LocalProfile
import com.taffygo.browser.ui.core.model.Tab

/**
 * Navigation leaves ports alone; the details pane and the name field are
 * local.
 *
 * Nothing here discards [YouUiState.nameDraft]. A completed write clears it,
 * because only then is the stored name the same text — so closing the pane
 * cannot lose what somebody typed, and a reader of this function can see
 * that without also reading the view model.
 */
internal fun reduceYou(state: YouUiState, intent: YouIntent): YouUiState = when (intent) {
    YouIntent.OpenDetails -> state.copy(detailsOpen = true)
    YouIntent.CloseDetails -> state.copy(detailsOpen = false)
    is YouIntent.Open -> if (intent.row == YouRow.PROFILE) {
        state.copy(detailsOpen = true)
    } else {
        state
    }
    is YouIntent.EditName ->
        state.copy(nameDraft = LocalProfile.boundedDisplayName(intent.text))
    YouIntent.CommitName,
    is YouIntent.ChooseAvatar,
    -> state
}

/** What the name field shows: the typed text until it has been stored. */
internal fun youNameField(state: YouUiState): String =
    state.nameDraft ?: state.displayName.orEmpty()

/**
 * The name to draw, or null when there is none to draw.
 *
 * It follows the field rather than the store, so the card above the field
 * agrees with it while somebody is still typing.
 */
internal fun youShownName(state: YouUiState): String? =
    (state.nameDraft ?: state.displayName)?.trim()?.takeIf { it.isNotEmpty() }

/** Projection from the hub's ports. Pure, so empty and unavailable are unit tests. */
internal fun projectYou(
    time: TimeOnSitesRepository.Snapshot,
    memory: MemoryRepository.Snapshot,
    signIns: SavedSignInsRepository.Snapshot,
    details: SavedDetailsRepository.Snapshot,
    tabs: List<Tab>,
    profile: LocalProfile,
    detailsOpen: Boolean,
    nameDraft: String?,
): YouUiState {
    // The monogram follows the field, not the store, so the letters in the
    // picker are the ones the name being typed would actually produce.
    val shown = nameDraft
        ?.let { profile.copy(displayName = LocalProfile.normalizedDisplayName(it)) }
        ?: profile
    return YouUiState(
        avatar = profile.avatar,
        monogram = shown.monogram,
        displayName = profile.displayName,
        nameDraft = nameDraft,
        detailsOpen = detailsOpen,
        privateTab = tabs.any { it.isSelected && it.isPrivate },
        timeAvailability = time.availability,
        timeHasSites = time.today.isNotEmpty() || time.week.isNotEmpty(),
        memoryAvailability = memory.availability,
        memoryCount = memory.notes.size,
        signInsAvailability = signIns.availability,
        signInsCount = signIns.records.size,
        detailsAvailability = details.availability,
        detailsCount = details.people.size,
    )
}
