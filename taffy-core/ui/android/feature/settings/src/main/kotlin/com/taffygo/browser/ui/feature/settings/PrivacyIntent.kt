// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-403 can be asked to do. */
sealed interface PrivacyIntent {
    data object OpenSiteSettings : PrivacyIntent
    data object OpenClearData : PrivacyIntent
    data object OpenWhatHappened : PrivacyIntent
    data object RequestExport : PrivacyIntent
    data object ExportDestinationCancelled : PrivacyIntent
    data object ExportDestinationFailed : PrivacyIntent
    data object ExportDestinationSelected : PrivacyIntent
    data class ExportFinished(val result: ProfileDataControl.ExportResult) : PrivacyIntent
    data object RequestDeleteEverything : PrivacyIntent
    data object DismissDeleteConfirmation : PrivacyIntent
    data object ConfirmDeleteEverything : PrivacyIntent
    data class DeleteFinished(val result: ProfileDataControl.DeletionResult) : PrivacyIntent
    data object Dismiss : PrivacyIntent
}
