// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-403 distinguishes a written export and an accepted erase request from completion. */
internal fun reducePrivacy(state: PrivacyUiState, intent: PrivacyIntent): PrivacyUiState =
    when (intent) {
        PrivacyIntent.RequestExport -> if (state.canStartExport) {
            state.copy(exportStatus = PrivacyUiState.ExportStatus.CHOOSING_DESTINATION)
        } else {
            state
        }
        PrivacyIntent.ExportDestinationCancelled -> if (
            state.exportStatus == PrivacyUiState.ExportStatus.CHOOSING_DESTINATION
        ) {
            state.copy(exportStatus = PrivacyUiState.ExportStatus.CANCELLED)
        } else {
            state
        }
        PrivacyIntent.ExportDestinationFailed -> if (
            state.exportStatus == PrivacyUiState.ExportStatus.CHOOSING_DESTINATION
        ) {
            state.copy(exportStatus = PrivacyUiState.ExportStatus.FAILED)
        } else {
            state
        }
        PrivacyIntent.ExportDestinationSelected -> if (
            state.exportStatus == PrivacyUiState.ExportStatus.CHOOSING_DESTINATION
        ) {
            state.copy(exportStatus = PrivacyUiState.ExportStatus.WRITING)
        } else {
            state
        }
        is PrivacyIntent.ExportFinished -> if (
            state.exportStatus == PrivacyUiState.ExportStatus.WRITING
        ) {
            state.copy(
                exportStatus = when (intent.result) {
                    ProfileDataControl.ExportResult.COMPLETED ->
                        PrivacyUiState.ExportStatus.SUCCEEDED
                    ProfileDataControl.ExportResult.UNAVAILABLE,
                    ProfileDataControl.ExportResult.FAILED,
                    -> PrivacyUiState.ExportStatus.FAILED
                },
            )
        } else {
            state
        }
        PrivacyIntent.RequestDeleteEverything -> if (state.canStartDeletion) {
            state.copy(deletionStatus = PrivacyUiState.DeletionStatus.CONFIRMING)
        } else {
            state
        }
        PrivacyIntent.DismissDeleteConfirmation -> if (
            state.deletionStatus == PrivacyUiState.DeletionStatus.CONFIRMING
        ) {
            state.copy(deletionStatus = PrivacyUiState.DeletionStatus.IDLE)
        } else {
            state
        }
        PrivacyIntent.ConfirmDeleteEverything -> if (
            state.deletionStatus == PrivacyUiState.DeletionStatus.CONFIRMING &&
                state.canStartDeletion
        ) {
            state.copy(deletionStatus = PrivacyUiState.DeletionStatus.STARTING)
        } else {
            state
        }
        is PrivacyIntent.DeleteFinished -> if (
            state.deletionStatus == PrivacyUiState.DeletionStatus.STARTING
        ) {
            state.copy(
                deletionStatus = when (intent.result) {
                    ProfileDataControl.DeletionResult.STARTED ->
                        PrivacyUiState.DeletionStatus.STARTED
                    ProfileDataControl.DeletionResult.UNAVAILABLE,
                    ProfileDataControl.DeletionResult.FAILED,
                    -> PrivacyUiState.DeletionStatus.FAILED
                },
            )
        } else {
            state
        }
        PrivacyIntent.OpenSiteSettings,
        PrivacyIntent.OpenClearData,
        PrivacyIntent.OpenWhatHappened,
        PrivacyIntent.Dismiss,
        -> state
    }
