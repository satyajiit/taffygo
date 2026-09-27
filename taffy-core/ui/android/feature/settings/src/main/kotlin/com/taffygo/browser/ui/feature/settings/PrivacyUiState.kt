// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.ProviderRoute

/** Screen SCR-403 — what's kept, where requests go, and how to clear it. */
data class PrivacyUiState(
    val exportAvailable: Boolean = false,
    val deleteAvailable: Boolean = false,
    val route: ProviderRoute = ProviderRoute.NOT_CONFIGURED,
    val savedWorkspaces: PrivacyDataCount = PrivacyDataCount.Loading,
    val libraryItems: PrivacyDataCount = PrivacyDataCount.Loading,
    val memoryItems: PrivacyDataCount = PrivacyDataCount.Loading,
    val connectedProviders: PrivacyDataCount = PrivacyDataCount.Loading,
    val recentDownloads: PrivacyDataCount = PrivacyDataCount.Loading,
    val changedSites: PrivacyDataCount = PrivacyDataCount.Loading,
    val exportStatus: ExportStatus = ExportStatus.IDLE,
    val deletionStatus: DeletionStatus = DeletionStatus.IDLE,
) {
    enum class ExportStatus { IDLE, CHOOSING_DESTINATION, WRITING, SUCCEEDED, FAILED, CANCELLED }

    enum class DeletionStatus { IDLE, CONFIRMING, STARTING, STARTED, FAILED }

    val canStartExport: Boolean
        get() = exportAvailable && exportStatus !in setOf(
            ExportStatus.CHOOSING_DESTINATION,
            ExportStatus.WRITING,
        ) && deletionStatus !in setOf(DeletionStatus.STARTING, DeletionStatus.STARTED)

    val canStartDeletion: Boolean
        get() = deleteAvailable && deletionStatus !in setOf(
            DeletionStatus.STARTING,
            DeletionStatus.STARTED,
        ) && exportStatus != ExportStatus.WRITING
}
