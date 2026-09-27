// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-413 — metadata only; password material is not a possible state. */
data class SavedSignInsUiState(
    val availability: YouSurfaceAvailability = YouSurfaceAvailability.UNAVAILABLE,
    val query: String = "",
    val records: List<SavedSignInsRepository.Record> = emptyList(),
    val opened: SavedSignInsRepository.Record? = null,
    val confirmDelete: Boolean = false,
) {
    val matching: List<SavedSignInsRepository.Record> by lazy(LazyThreadSafetyMode.NONE) {
        val trimmed = query.trim()
        if (trimmed.isEmpty()) {
            records
        } else {
            records.filter { record ->
                record.site.contains(trimmed, ignoreCase = true) ||
                    record.username.contains(trimmed, ignoreCase = true)
            }
        }
    }
}
