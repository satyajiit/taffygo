// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.ExportFormat

/** Everything screen SCR-309 can be asked to do. */
sealed interface ExportSheetIntent {

    /** Choose a format. */
    data class Select(val format: ExportFormat) : ExportSheetIntent

    /** Write the file through the system document picker. */
    data object Export : ExportSheetIntent

    /** The system returned a writable destination. */
    data object DestinationSelected : ExportSheetIntent

    /** The system picker closed without a destination. */
    data object DestinationCancelled : ExportSheetIntent

    /** The exact Rust-rendered bytes reached the selected document. */
    data object WriteSucceeded : ExportSheetIntent

    /** The selected document could not be opened or fully written. */
    data object WriteFailed : ExportSheetIntent

    /** Leave without writing anything. */
    data object Close : ExportSheetIntent
}
